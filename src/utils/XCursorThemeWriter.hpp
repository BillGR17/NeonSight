#pragma once

#include <string>

namespace NeonSight {

/// Writes a custom XCursor icon theme to ~/.local/share/icons/NeonSight/
/// so Wayland compositors can pick it up via runtime theme-switch commands.
class XCursorThemeWriter {
public:
  static constexpr const char *kThemeName = "NeonSight";

  /// Write all cursor name files and index.theme for the given SVG/PNG.
  /// Returns true on success.
  static bool Write(const std::string &imagePath, int size, float hotXPercent, float hotYPercent);

  /// Remove the cursor theme directory entirely (called on clean exit).
  static void Remove();

  /// Returns the full path to the theme directory.
  static std::string ThemeDir();
};

} // namespace NeonSight
