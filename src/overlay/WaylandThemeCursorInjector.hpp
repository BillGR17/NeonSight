#pragma once

#include "ICursorInjector.hpp"
#include <string>

namespace NeonSight {

class WaylandThemeCursorInjector : public ICursorInjector {
public:
  WaylandThemeCursorInjector();
  ~WaylandThemeCursorInjector() override;

  bool Initialize() override;
  void ApplyCursor() override;
  bool LoadCursor(const std::string &cursorData, int fallbackSize, float hotXPercent, float hotYPercent) override;
  void SetActive(bool active) override;
  [[nodiscard]] bool IsActive() const override;
  void RestoreAll() override;
  [[nodiscard]] bool IsRunning() const override;

private:
  enum class CompositorType { Generic, GNOME, KDE, Hyprland };

  void ApplyThemeSystemWide();

  std::string defaultTheme_{"Adwaita"};
  int defaultSize_{24};
  bool isActive_{false};
  bool applied_{false}; // whether the NeonSight theme is actually applied system-wide
  int currentSize_{32};
  CompositorType compositor_{CompositorType::Generic};
};

} // namespace NeonSight
