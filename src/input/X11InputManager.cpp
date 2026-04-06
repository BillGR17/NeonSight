#include "X11InputManager.hpp"
#include <iostream>

namespace NeonSight {

X11InputManager::X11InputManager() = default;

X11InputManager::~X11InputManager() {
  if (display_) {
    XCloseDisplay(display_);
  }
}

bool X11InputManager::Initialize() {
  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    std::cerr << "Failed to open X11 Display." << std::endl;
    return false;
  }
  rootWindow_ = DefaultRootWindow(display_);
  return true;
}

void X11InputManager::Update() {
  // For hotkeys, we would XNextEvent here.
}

Point X11InputManager::GetMousePosition() const {
  if (!display_)
    return {0, 0};

  Window root_ret, child_ret;
  int root_x, root_y, win_x, win_y;
  unsigned int mask;

  if (XQueryPointer(display_, rootWindow_, &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask)) {
    return {root_x, root_y};
  }

  return {0, 0};
}

void X11InputManager::RegisterHotkey(int /*keycode*/, HotkeyCallback /*callback*/) {
  // Stub definition for future XGrabKey setup
}

} // namespace NeonSight
