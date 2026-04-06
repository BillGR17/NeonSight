#include "XCursorThemeWriter.hpp"
#include "ImageLoader.hpp"

#include <X11/Xcursor/Xcursor.h>

#include <iostream>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <cstdlib>

namespace NeonSight {

// Standard X11/Wayland cursor names. All map to the same custom image.
static const char *kCursorNames[] = {"default",    "left_ptr",    "arrow",     "center_ptr", "crosshair",      "diamond_cross", "cross",
                                     "tcross",     "right_ptr",   "pointer",   "hand1",      "hand2",          "grab",          "grabbing",
                                     "move",       "fleur",       "size_all",  "wait",       "watch",          "progress",      "half-busy",
                                     "text",       "ibeam",       "xterm",     "help",       "question_arrow", "n-resize",      "s-resize",
                                     "e-resize",   "w-resize",    "ne-resize", "nw-resize",  "se-resize",      "sw-resize",     "col-resize",
                                     "row-resize", "not-allowed", "no-drop",   "zoom-in",    "zoom-out",       nullptr};

// index.theme content
static constexpr const char *kIndexTheme = "[Icon Theme]\n"
                                           "Name=NeonSight\n"
                                           "Comment=NeonSight custom high-visibility game cursor\n"
                                           "Inherits=hicolor\n";

// Helper Functions

std::string XCursorThemeWriter::ThemeDir() {
  const char *home = std::getenv("HOME");
  std::string base = home ? std::string(home) : ".";
  return base + "/.local/share/icons/" + kThemeName;
}

/// Write one XCursor binary file for the given image.
static bool WriteCursorFile(const std::string &destPath, XcursorImage *img) {
  FILE *f = fopen(destPath.c_str(), "wb");
  if (!f) {
    std::cerr << "XCursorThemeWriter: fopen failed: " << destPath << " — " << strerror(errno) << "\n";
    return false;
  }

  // XcursorFileSaveImages: no comments arg needed, simpler than XcursorFileSave
  XcursorImages *images = XcursorImagesCreate(1);
  if (!images) {
    std::cerr << "XCursorThemeWriter: XcursorImagesCreate returned null\n";
    fclose(f);
    return false;
  }

  images->images[0] = img;
  images->nimage = 1;

  XcursorBool ok = XcursorFileSaveImages(f, images);
  if (!ok) {
    std::cerr << "XCursorThemeWriter: XcursorFileSaveImages failed for: " << destPath << "\n";
  }

  // Nullify before destroy so the library doesn't double-free img
  images->images[0] = nullptr;
  XcursorImagesDestroy(images);

  fclose(f);
  return ok != 0;
}

// Public API

bool XCursorThemeWriter::Write(const std::string &imagePath, int size, float hotXPercent, float hotYPercent) {
  const std::string themeDir = ThemeDir();
  const std::string cursorsDir = themeDir + "/cursors";

  // 1. Create directories (creates all parents as needed)
  std::error_code ec;
  std::filesystem::create_directories(cursorsDir, ec);
  if (ec) {
    std::cerr << "XCursorThemeWriter: Failed to create theme dirs: " << cursorsDir << " — " << ec.message() << "\n";
    return false;
  }

  // 2. Write index.theme
  {
    std::ofstream ofs(themeDir + "/index.theme");
    if (!ofs) {
      std::cerr << "XCursorThemeWriter: Cannot write index.theme in " << themeDir << "\n";
      return false;
    }
    ofs << kIndexTheme;
  }

  // 3. Load the source image once
  XcursorImage *src = ImageLoader::LoadAsXcursor(imagePath, size, hotXPercent, hotYPercent);
  if (!src) {
    std::cerr << "XCursorThemeWriter: LoadAsXcursor failed for: " << imagePath << "\n";
    return false;
  }

  std::cout << "XCursorThemeWriter: Writing theme to " << cursorsDir << " (" << src->width << "x" << src->height << ")\n";

  // 4. Write one binary cursor file per standard name
  int failCount = 0;
  for (int i = 0; kCursorNames[i] != nullptr; ++i) {
    const std::string dest = cursorsDir + "/" + kCursorNames[i];

    // Build a copy with delay=50ms (required by XCursor format — 0 can be rejected)
    XcursorImage *copy = XcursorImageCreate(src->width, src->height);
    if (!copy) {
      std::cerr << "XCursorThemeWriter: XcursorImageCreate failed for: " << kCursorNames[i] << "\n";
      ++failCount;
      continue;
    }
    copy->xhot = src->xhot;
    copy->yhot = src->yhot;
    copy->size = src->size;
    copy->delay = 0;
    std::memcpy(copy->pixels, src->pixels, src->width * src->height * sizeof(XcursorPixel));

    if (!WriteCursorFile(dest, copy)) {
      ++failCount;
    }
    XcursorImageDestroy(copy);
  }

  XcursorImageDestroy(src);

  if (failCount > 0) {
    std::cerr << "XCursorThemeWriter: " << failCount << " cursor file(s) failed to write.\n";
    return false;
  }

  std::cout << "XCursorThemeWriter: Theme written successfully to " << themeDir << "\n";
  return true;
}

void XCursorThemeWriter::Remove() {
  const std::string dir = ThemeDir();
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  if (ec) {
    std::cerr << "XCursorThemeWriter: Failed to remove theme dir: " << ec.message() << "\n";
  } else {
    std::cout << "XCursorThemeWriter: Theme directory removed.\n";
  }
}

} // namespace NeonSight
