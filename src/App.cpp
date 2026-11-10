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

  // Bring up an injector, keeping it only if it initialized successfully.
  auto tryAddInjector = [this](std::unique_ptr<ICursorInjector> injector) -> bool {
    if (injector->Initialize()) {
      injectors_.push_back(std::move(injector));
      return true;
    }
    return false;
  };

  if (isWayland) {
    // Native Wayland games can only be reached through the compositor's cursor —
    // that's the theme injector.
    std::cout << "Starting NeonSight Wayland theme injector (native Wayland games)...\n";
    tryAddInjector(std::make_unique<WaylandThemeCursorInjector>());

    // X11/XWayland games run on an X server (XWayland), so the hardware injector
    // works there exactly as it does on a pure X11 session. Best-effort: if no X
    // display is reachable we skip it instead of failing the whole app.
    std::cout << "Starting NeonSight X11/XWayland hardware injector...\n";
    if (!tryAddInjector(std::make_unique<X11CursorInjector>())) {
      std::cerr << "Note: no X/XWayland display reachable — X11/XWayland game injection is disabled.\n";
    }
  } else {
    std::cout << "Starting NeonSight X11 overlay injector...\n";
    tryAddInjector(std::make_unique<X11CursorInjector>());
  }

  // The input manager is optional (global hotkeys are a stub today). On a pure
  // Wayland session there may be no X display, so a failure must NOT prevent the
  // app from starting — clear the pointer and continue.
  if (!inputManager_->Initialize()) {
    std::cerr << "Note: X11 input manager unavailable (no X display). Continuing without global hotkeys.\n";
    inputManager_.reset();
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

  // Toggle the config window. These run on the MAIN thread (see PumpActions()),
  // so the GLFW show/hide calls inside are safe.
  auto toggleWindow = [this]() {
    if (!uiWindow_)
      return;
    if (uiWindow_->IsVisible())
      uiWindow_->Hide();
    else
      uiWindow_->Show();
  };

  // Left-click the tray -> toggle the window.
  trayIcon_ = std::make_unique<TrayIcon>(toggleWindow);

  // Right-click tray -> context menu. Item ids match TrayIcon's GetLayout
  // (1 = Show/Hide, 2 = Quit); dispatched on the main thread.
  trayIcon_->SetMenuActionCallback([this, toggleWindow](int id) {
    switch (id) {
      case 1:
        toggleWindow();
        break;
      case 2:
        std::cout << "Tray menu: Quit requested.\n";
        if (uiWindow_)
          uiWindow_->Quit(); // -> Render() returns false -> clean shutdown + restore
        break;
      default:
        break;
    }
  });

  if (!trayIcon_->Initialize()) {
    std::cerr << "Warning: Failed to initialize Tray Icon. Tray clicks will not work.\n";
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

    // Run any tray clicks that arrived on the DBus/X11 worker thread, here on the
    // main thread so the show/hide (GLFW) calls inside them are safe.
    if (trayIcon_) {
      trayIcon_->PumpActions();
    }

    // Let UI handle its events & draw calls
    if (!uiWindow_->Render()) {
      std::cout << "UI requested quit. Initiating shutdown...\n";
      isRunning_ = false;
      break;
    }

    // Process hardware inputs (optional — not present on pure Wayland sessions)
    if (inputManager_) {
      inputManager_->Update();
    }

    // Push cursor into active window
    for (auto &injector : injectors_) {
      injector->ApplyCursor();
    }

    // Frame pacing is handled by vsync (glfwSwapInterval(1) in ConfigWindow).
    // An extra sleep here would only add input/UI latency without saving CPU.
  }
}

} // namespace NeonSight
