#include "App.hpp"
#include "input/X11InputManager.hpp"
#include "overlay/X11CursorInjector.hpp"
#include "overlay/WaylandThemeCursorInjector.hpp"
#include "ui/ConfigWindow.hpp"
#include "ui/TrayIcon.hpp"
#include "config/AppConfig.hpp"

#include <iostream>
#include <cstdlib>
#include <filesystem>
#include "config/CursorMeta.hpp"

namespace NeonSight {

App::App() = default;

App::~App() {
  if (!injectors_.empty()) {
    std::cout << "Restoring modified cursors back to defaults..." << std::endl;
    for (auto &injector : injectors_) {
      injector->RestoreAll();
    }
  }
}

bool App::Initialize(bool forceX11) {
  bool isWayland = false;
  if (!forceX11) {
    const char *sessionType = std::getenv("XDG_SESSION_TYPE");
    const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if ((sessionType && std::string(sessionType) == "wayland") || waylandDisplay) {
      isWayland = true;
    }
  }

  inputManager_ = std::make_unique<X11InputManager>();

  if (isWayland) {
    std::cout << "Starting NeonSight Native Wayland Native Theme injector...\n";
    auto waylandInjector = std::make_unique<WaylandThemeCursorInjector>();
    if (waylandInjector->Initialize()) {
      injectors_.push_back(std::move(waylandInjector));
    }
  } else {
    std::cout << "Starting NeonSight X11 overlay injector...\n";
    auto x11Injector = std::make_unique<X11CursorInjector>();
    if (x11Injector->Initialize()) {
      injectors_.push_back(std::move(x11Injector));
    }
  }

  if (!inputManager_->Initialize()) {
    std::cerr << "Failed to initialize input manager.\n";
    return false;
  }

  if (injectors_.empty()) {
    std::cerr << "Failed to initialize any cursor injectors.\n";
    return false;
  }

  // Try loading config initially
  AppConfig::Get().Load();
  if (!AppConfig::Get().activeCursor.empty()) {
    const char *home = std::getenv("HOME");
    std::string cursorDir = (home ? std::string(home) : ".") + "/.config/NeonSight/cursors/";
    std::string fullPath = cursorDir + AppConfig::Get().activeCursor;

    if (!std::filesystem::exists(fullPath)) {
      // Saved cursor file was deleted externally — clear the stale entry
      std::cerr << "Saved cursor not found, resetting: " << fullPath << '\n';
      AppConfig::Get().activeCursor.clear();
      AppConfig::Get().isCursorEnabled = false;
      AppConfig::Get().Save();
    } else {
      // Load hotspot from per-cursor sidecar JSON
      std::string stem = std::filesystem::path(AppConfig::Get().activeCursor).stem().string();
      std::string metaPath = cursorDir + stem + ".json";

      CursorMeta meta;
      meta.Load(metaPath);

      for (auto &injector : injectors_) {
        injector->SetActive(AppConfig::Get().isCursorEnabled);
        if (AppConfig::Get().isCursorEnabled) {
          injector->LoadCursor(fullPath, AppConfig::Get().cursorSize, meta.hotspotX, meta.hotspotY);
        }
      }
    }
  }

  // Setup the ImGui window
  uiWindow_ = std::make_unique<ConfigWindow>();
  if (!uiWindow_->Initialize()) {
    std::cerr << "Failed to initialize the configuration UI window.\n";
    return false;
  }
  uiWindow_->Show();

  // Hook up the UI callback to push the parsed file directly into the injector
  uiWindow_->SetOnConfigChangedCallback([this](const std::string &filepath, int size, bool enabled, float hotX, float hotY) {
    std::cout << "UI Callback: Current config: " << filepath << " (size: " << size << ", enabled: " << (enabled ? "yes" : "no") << ")\n";
    for (auto &injector : injectors_) {
      if (enabled) {
        injector->SetActive(true);
        injector->LoadCursor(filepath, size, hotX, hotY);
      } else {
        injector->SetActive(false);
        injector->RestoreAll(); // To immediately clear existing custom cursor overrides
      }
    }
  });

  trayIcon_ = std::make_unique<TrayIcon>([this]() {
    if (uiWindow_) {
      if (uiWindow_->IsVisible()) {
        uiWindow_->Hide();
      } else {
        uiWindow_->Show();
      }
    }
  });

  if (!trayIcon_->Initialize()) {
    std::cerr << "Warning: Failed to initialize Tray Icon. Minimize to tray will not work nicely.\n";
  }

  isRunning_ = true;
  return true;
}

void App::Run(const std::atomic<bool> &globalRunningFlag) {
  std::cout << "NeonSight is running! Configuration UI opened.\n";
  std::cout << "Close the window or press Ctrl+C to terminate.\n";

  // Combined loop for ImGui and hardware cursor injection polling
  while (isRunning_ && globalRunningFlag) {
    bool anyRunning = false;
    for (auto &injector : injectors_) {
      if (injector->IsRunning()) {
        anyRunning = true;
        break;
      }
    }
    if (!anyRunning)
      break;

    // Let UI handle its events & draw calls
    if (!uiWindow_->Render()) {
      std::cout << "UI requested quit. Initiating shutdown...\n";
      isRunning_ = false;
      break;
    }

    // Process hardware inputs
    inputManager_->Update();

    // Push cursor into active window
    for (auto &injector : injectors_) {
      injector->ApplyCursor();
    }

    // Frame pacing is handled by vsync (glfwSwapInterval(1) in ConfigWindow).
    // An extra sleep here would only add input/UI latency without saving CPU.
  }
}

} // namespace NeonSight
