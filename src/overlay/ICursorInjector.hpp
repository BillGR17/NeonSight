#pragma once

#include <string>

namespace NeonSight {

class ICursorInjector {
public:
  virtual ~ICursorInjector() = default;

  virtual bool Initialize() = 0;

  // Apply the loaded cursor to whatever is under the global pointer (if active)
  virtual void ApplyCursor() = 0;

  // Set the cursor data
  virtual bool LoadCursor(const std::string &cursorData, int fallbackSize = 48, float hotXPercent = 0.5f, float hotYPercent = 0.5f) = 0;

  // Turn injection on or off
  virtual void SetActive(bool active) = 0;
  [[nodiscard]] virtual bool IsActive() const = 0;

  // Restore all original cursors that were modified
  virtual void RestoreAll() = 0;

  [[nodiscard]] virtual bool IsRunning() const = 0;
};

} // namespace NeonSight
