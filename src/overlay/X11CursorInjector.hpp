#pragma once
#include "ICursorInjector.hpp"

#include <string>
#include <X11/Xlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>

namespace NeonSight {

class X11CursorInjector : public ICursorInjector {
public:
  X11CursorInjector();
  ~X11CursorInjector() override;

  bool Initialize() override;
  void ApplyCursor() override;
  bool LoadCursor(const std::string &cursorData, int fallbackSize = 48, float hotXPercent = 0.5f, float hotYPercent = 0.5f) override;

  void SetActive(bool active) override;
  [[nodiscard]] bool IsActive() const override;
  void RestoreAll() override;

  [[nodiscard]] bool IsRunning() const override;

private:
  bool LoadCursor(XcursorImage *image);
  Window FindLeafWindow(Window root, int rootX, int rootY);

  Display *display_{nullptr};
  Window rootWindow_{None};

  Cursor currentCursor_{None};
  Window lastLeaf_{None};
  Window lastInjectedWindow_{None};

  bool isActive_{false};

  static constexpr int kMaxSearchDepth = 16;
};

} // namespace NeonSight
