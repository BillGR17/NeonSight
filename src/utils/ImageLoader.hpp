#pragma once

#include <string>
#include <optional>
#include <vector>

#ifdef __linux__
#include <X11/Xcursor/Xcursor.h>
#endif

// Forward declare OpenGL texture ID type
typedef unsigned int GLuint;

namespace NeonSight {

class ImageLoader {
public:
  // Load a PNG/SVG and convert to X11 ARGB format for hardware cursors
  static XcursorImage *LoadAsXcursor(const std::string &filepath, int fallbackSize = 48, float hotXPercent = 0.5f, float hotYPercent = 0.5f);

  // Load a PNG and upload it to the GPU, returning its OpenGL Texture ID for ImGui binding
  static GLuint LoadAsTexture(const std::string &filepath, int &outWidth, int &outHeight);

  // Load a raw RGBA buffer (owned by the caller's std::vector, no manual free needed).
  // Returns std::nullopt if the image cannot be decoded.
  static std::optional<std::vector<unsigned char>> LoadRaw(const std::string &filepath, int &outWidth, int &outHeight, int requestedSize = 64);
  static std::optional<std::vector<unsigned char>> LoadRawFromMemory(const unsigned char *data, unsigned int size, int &outWidth, int &outHeight, int requestedSize = 64);
};

} // namespace NeonSight
