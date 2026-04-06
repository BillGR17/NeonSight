#pragma once
#include "IInputManager.hpp"
#include <X11/Xlib.h>

namespace NeonSight {

class X11InputManager : public IInputManager {
public:
  X11InputManager();
  ~X11InputManager() override;

  bool Initialize() override;
  void Update() override;
  [[nodiscard]] Point GetMousePosition() const override;
  void RegisterHotkey(int keycode, HotkeyCallback callback) override;

private:
  Display *display_{nullptr};
  Window rootWindow_;
};

} // namespace NeonSight
