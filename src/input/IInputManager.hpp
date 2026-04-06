#pragma once

#include <functional>

namespace NeonSight {

struct Point {
  int x;
  int y;
};

class IInputManager {
public:
  virtual ~IInputManager() = default;

  // Initialize the input hook
  virtual bool Initialize() = 0;

  // Pump events (should be called in app loop)
  virtual void Update() = 0;

  // Get current global mouse position
  [[nodiscard]] virtual Point GetMousePosition() const = 0;

  // Register a hotkey (simple for now: true if pressed)
  // Could accept keycode and callback
  using HotkeyCallback = std::function<void()>;
  virtual void RegisterHotkey(int keycode, HotkeyCallback callback) = 0;
};

} // namespace NeonSight
