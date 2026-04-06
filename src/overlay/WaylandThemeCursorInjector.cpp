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

  if (compositor_ == CompositorType::GNOME) {
    system("gsettings set org.gnome.desktop.interface cursor-theme NeonSight 2>/dev/null");
    system(("gsettings set org.gnome.desktop.interface cursor-size " + std::to_string(currentSize_) + " 2>/dev/null").c_str());
  } else if (compositor_ == CompositorType::KDE) {
    system("kwriteconfig6 --group \"Mouse\" --key \"cursorTheme\" NeonSight 2>/dev/null");
    system(("kwriteconfig6 --group \"Mouse\" --key \"cursorSize\" " + std::to_string(currentSize_) + " 2>/dev/null").c_str());
    // Notify KDE of changes
    system("dbus-send --type=signal /KWin org.kde.KWin.reloadConfig 2>/dev/null");
  }
}

void WaylandThemeCursorInjector::RestoreAll() {
  if (!isActive_)
    return;

  std::cout << "WaylandThemeCursorInjector: Restoring default system cursor: " << defaultTheme_ << "\n";

  if (compositor_ == CompositorType::GNOME) {
    system(("gsettings set org.gnome.desktop.interface cursor-theme " + defaultTheme_ + " 2>/dev/null").c_str());
    system(("gsettings set org.gnome.desktop.interface cursor-size " + std::to_string(defaultSize_) + " 2>/dev/null").c_str());
  } else if (compositor_ == CompositorType::KDE) {
    system(("kwriteconfig6 --group \"Mouse\" --key \"cursorTheme\" " + defaultTheme_ + " 2>/dev/null").c_str());
    system(("kwriteconfig6 --group \"Mouse\" --key \"cursorSize\" " + std::to_string(defaultSize_) + " 2>/dev/null").c_str());
    system("dbus-send --type=signal /KWin org.kde.KWin.reloadConfig 2>/dev/null");
  }

  XCursorThemeWriter::Remove();
  isActive_ = false;
}

bool WaylandThemeCursorInjector::IsRunning() const { return true; }

} // namespace NeonSight
