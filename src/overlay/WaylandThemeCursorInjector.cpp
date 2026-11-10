#include "WaylandThemeCursorInjector.hpp"
#include "../utils/XCursorThemeWriter.hpp"
#include <iostream>
#include <cstdlib>
#include <string>
#include <memory>
#include <array>

namespace NeonSight {

static std::string ExecCommand(const char *cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
  if (!pipe)
    return "";
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  if (!result.empty() && result.back() == '\n')
    result.pop_back();
  if (!result.empty() && result.front() == '\'' && result.back() == '\'') {
    result = result.substr(1, result.size() - 2);
  }
  return result;
}

WaylandThemeCursorInjector::WaylandThemeCursorInjector() = default;

WaylandThemeCursorInjector::~WaylandThemeCursorInjector() { RestoreAll(); }

bool WaylandThemeCursorInjector::Initialize() {
  std::cout << "WaylandThemeCursorInjector: Initializing native compositor theme injector.\n";

  const char *xdgDesktop = std::getenv("XDG_CURRENT_DESKTOP");
  std::string desktop = xdgDesktop ? xdgDesktop : "";

  if (desktop.find("GNOME") != std::string::npos) {
    compositor_ = CompositorType::GNOME;
  } else if (desktop.find("KDE") != std::string::npos) {
    compositor_ = CompositorType::KDE;
  } else if (desktop.find("Hyprland") != std::string::npos) {
    compositor_ = CompositorType::Hyprland;
  }

  // Retrieve current defaults based on likely toolset
  if (compositor_ == CompositorType::GNOME) {
    std::string theme = ExecCommand("gsettings get org.gnome.desktop.interface cursor-theme 2>/dev/null");
    if (!theme.empty() && theme != "NeonSight")
      defaultTheme_ = theme;

    std::string sizeParams = ExecCommand("gsettings get org.gnome.desktop.interface cursor-size 2>/dev/null");
    if (!sizeParams.empty()) {
      try {
        defaultSize_ = std::stoi(sizeParams);
      } catch (...) {
      }
    }
  } else if (compositor_ == CompositorType::KDE) {
    std::string theme = ExecCommand("kreadconfig6 --group \"Mouse\" --key \"cursorTheme\" 2>/dev/null");
    if (!theme.empty() && theme != "NeonSight")
      defaultTheme_ = theme;

    std::string sizeParams = ExecCommand("kreadconfig6 --group \"Mouse\" --key \"cursorSize\" 2>/dev/null");
    if (!sizeParams.empty()) {
      try {
        defaultSize_ = std::stoi(sizeParams);
      } catch (...) {
      }
    }
  } else if (compositor_ == CompositorType::Hyprland) {
    // Hyprland has no clean "get current cursor" query. It reads the cursor from
    // HYPRCURSOR_THEME / XCURSOR_THEME (set in the session), so capture those as
    // the restore target, falling back to Hyprland's built-in default cursor.
    const char *hyprTheme = std::getenv("HYPRCURSOR_THEME");
    const char *xcTheme = std::getenv("XCURSOR_THEME");
    if (hyprTheme && std::string(hyprTheme) != "NeonSight")
      defaultTheme_ = hyprTheme;
    else if (xcTheme && std::string(xcTheme) != "NeonSight")
      defaultTheme_ = xcTheme;
    else
      defaultTheme_ = "default";

    const char *hyprSize = std::getenv("HYPRCURSOR_SIZE");
    const char *xcSize = std::getenv("XCURSOR_SIZE");
    const char *sizeEnv = (hyprSize && *hyprSize) ? hyprSize : xcSize;
    if (sizeEnv) {
      try {
        defaultSize_ = std::stoi(sizeEnv);
      } catch (...) {
      }
    }
  }

  return true;
}

void WaylandThemeCursorInjector::ApplyCursor() {
  // Handled natively by Wayland compositor, no per-frame polling required.
}

bool WaylandThemeCursorInjector::LoadCursor(const std::string &cursorPath, int size, float hotXPercent, float hotYPercent) {
  currentSize_ = size > 0 ? size : 32;

  if (!XCursorThemeWriter::Write(cursorPath, currentSize_, hotXPercent, hotYPercent)) {
    return false;
  }

  if (isActive_) {
    ApplyThemeSystemWide();
  }
  return true;
}

void WaylandThemeCursorInjector::SetActive(bool active) {
  isActive_ = active;
  if (isActive_) {
    ApplyThemeSystemWide();
  } else {
    RestoreAll();
  }
}

bool WaylandThemeCursorInjector::IsActive() const { return isActive_; }

void WaylandThemeCursorInjector::ApplyThemeSystemWide() {
  std::cout << "WaylandThemeCursorInjector: Applying NeonSight theme system-wide.\n";

  // The theme is ALWAYS named "NeonSight" on disk — only the image inside changes
  // when you pick a different cursor. Compositor themes are cached by name, so an
  // identical "set NeonSight" is a no-op and the old image stays. We therefore
  // switch to the saved default theme first (breaking the cache), then back to
  // NeonSight, forcing a fresh read of the current files.
  if (compositor_ == CompositorType::GNOME) {
    system(("gsettings set org.gnome.desktop.interface cursor-theme " + defaultTheme_ + " 2>/dev/null").c_str());
    system("gsettings set org.gnome.desktop.interface cursor-theme NeonSight 2>/dev/null");
    system(("gsettings set org.gnome.desktop.interface cursor-size " + std::to_string(currentSize_) + " 2>/dev/null").c_str());
  } else if (compositor_ == CompositorType::KDE) {
    system(("kwriteconfig6 --group \"Mouse\" --key \"cursorTheme\" " + defaultTheme_ + " 2>/dev/null").c_str());
    system("kwriteconfig6 --group \"Mouse\" --key \"cursorTheme\" NeonSight 2>/dev/null");
    system(("kwriteconfig6 --group \"Mouse\" --key \"cursorSize\" " + std::to_string(currentSize_) + " 2>/dev/null").c_str());
    // Notify KDE of changes
    system("dbus-send --type=signal /KWin org.kde.KWin.reloadConfig 2>/dev/null");
  } else if (compositor_ == CompositorType::Hyprland) {
    // hyprctl setcursor is Hyprland's live cursor API. Switch away then back so
    // it re-reads the updated theme files (an identical re-set is otherwise a
    // no-op on an already-loaded theme).
    system(("hyprctl setcursor " + defaultTheme_ + " " + std::to_string(defaultSize_) + " 2>/dev/null").c_str());
    std::string cmd = "hyprctl setcursor NeonSight " + std::to_string(currentSize_);
    int rc = system(cmd.c_str());
    if (rc != 0) {
      std::cerr << "Note: Hyprland could not apply the cursor live. Restart Hyprland to pick up the installed theme.\n";
    }
  }

  applied_ = true;
}

void WaylandThemeCursorInjector::RestoreAll() {
  // Only touch the system cursor if we actually changed it. This is gated on
  // `applied_`, NOT `isActive_` — SetActive(false) clears isActive_ before
  // calling us, so guarding on it here silently skipped the restore.
  if (!applied_) {
    XCursorThemeWriter::Remove(); // still drop any stale theme dir
    return;
  }

  std::cout << "WaylandThemeCursorInjector: Restoring default system cursor: " << defaultTheme_ << "\n";

  if (compositor_ == CompositorType::GNOME) {
    system(("gsettings set org.gnome.desktop.interface cursor-theme " + defaultTheme_ + " 2>/dev/null").c_str());
    system(("gsettings set org.gnome.desktop.interface cursor-size " + std::to_string(defaultSize_) + " 2>/dev/null").c_str());
  } else if (compositor_ == CompositorType::KDE) {
    system(("kwriteconfig6 --group \"Mouse\" --key \"cursorTheme\" " + defaultTheme_ + " 2>/dev/null").c_str());
    system(("kwriteconfig6 --group \"Mouse\" --key \"cursorSize\" " + std::to_string(defaultSize_) + " 2>/dev/null").c_str());
    system("dbus-send --type=signal /KWin org.kde.KWin.reloadConfig 2>/dev/null");
  } else if (compositor_ == CompositorType::Hyprland) {
    std::string cmd = "hyprctl setcursor " + defaultTheme_ + " " + std::to_string(defaultSize_);
    system(cmd.c_str());
  }

  XCursorThemeWriter::Remove();
  applied_ = false;
  isActive_ = false;
}

bool WaylandThemeCursorInjector::IsRunning() const { return true; }

} // namespace NeonSight
