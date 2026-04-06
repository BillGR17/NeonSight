#pragma once

#include <string>

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

  // Load a raw RGBA buffer (caller must free with FreeRaw)
  static unsigned char *LoadRaw(const std::string &filepath, int &outWidth, int &outHeight, int requestedSize = 64);
  static unsigned char *LoadRawFromMemory(const unsigned char *data, unsigned int size, int &outWidth, int &outHeight, int requestedSize = 64);
  static void FreeRaw(unsigned char *data);
};

} // namespace NeonSight
