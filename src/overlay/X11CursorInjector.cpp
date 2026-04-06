#include "X11CursorInjector.hpp"
#include "../utils/ImageLoader.hpp"

#include <iostream>
#include <cstring>

namespace NeonSight {

static int XErrorHandlerSilent(Display *, XErrorEvent *) { return 0; }

X11CursorInjector::X11CursorInjector() = default;

X11CursorInjector::~X11CursorInjector() {
  RestoreAll();
  if (currentCursor_ != None && display_) {
    XFreeCursor(display_, currentCursor_);
  }
  if (display_) {
    XCloseDisplay(display_);
  }
}

bool X11CursorInjector::Initialize() {
  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    std::cerr << "X11CursorInjector: Failed to connect to X server.\n";
    return false;
  }
  rootWindow_ = DefaultRootWindow(display_);

  std::cout << "X11CursorInjector initialized successfully for pure hardware injection.\n";
  return true;
}

Window X11CursorInjector::FindLeafWindow(Window root, int /*rootX*/, int /*rootY*/) {
  Window current = root;
  int depth = 0;
  while (depth < kMaxSearchDepth) {
    Window rootReturn, childReturn;
    int dummyX, dummyY;
    unsigned int mask;
    auto prev = XSetErrorHandler(XErrorHandlerSilent);
    Bool ok = XQueryPointer(display_, current, &rootReturn, &childReturn, &dummyX, &dummyY, &dummyX, &dummyY, &mask);
    XSetErrorHandler(prev);

    if (!ok || childReturn == None) {
      break;
    }
    current = childReturn;
    depth++;
  }
  return current;
}

void X11CursorInjector::ApplyCursor() {
  if (!display_ || !isActive_ || currentCursor_ == None)
    return;

  Window rootRet, childRet;
  int rootX, rootY, winX, winY;
  unsigned int mask;

  auto prev = XSetErrorHandler(XErrorHandlerSilent);
  Bool ok = XQueryPointer(display_, rootWindow_, &rootRet, &childRet, &rootX, &rootY, &winX, &winY, &mask);
  XSetErrorHandler(prev);

  if (!ok)
    return;

  Window leaf = FindLeafWindow(rootWindow_, rootX, rootY);
  if (leaf != None && leaf != lastLeaf_) {
    // Restore previous child window's cursor
    if (lastLeaf_ != None && lastLeaf_ != rootWindow_) {
      XUndefineCursor(display_, lastLeaf_);
    }

    // Only define on child leaf windows; root is handled by LoadCursor
    if (leaf != rootWindow_) {
      XDefineCursor(display_, leaf, currentCursor_);
    }

    lastLeaf_ = leaf;
    lastInjectedWindow_ = leaf;
    XFlush(display_);
  }
}

bool X11CursorInjector::LoadCursor(XcursorImage *image) {
  if (!display_ || !image)
    return false;

  if (currentCursor_ != None) {
    XFreeCursor(display_, currentCursor_);
    currentCursor_ = None;
  }

  // Instantly allocate an X lib hardware Cursor directly bounded to GPU
  currentCursor_ = XcursorImageLoadCursor(display_, image);
  XcursorImageDestroy(image);

  lastLeaf_ = None;

  if (currentCursor_ == None) {
    std::cerr << "X11CursorInjector: Failed to upload cursor to X Server hardware plane.\n";
    return false;
  }

  // Apply to root immediately once
  XDefineCursor(display_, rootWindow_, currentCursor_);
  XFlush(display_);

  if (isActive_) {
    ApplyCursor();
  }

  return true;
}

bool X11CursorInjector::LoadCursor(const std::string &cursorPath, int size, float hotXPercent, float hotYPercent) {
  const auto hasExt = [&](const char *ext) {
    auto p = cursorPath.rfind(ext);
    return p != std::string::npos && p == cursorPath.size() - strlen(ext);
  };

  if (hasExt(".png") || hasExt(".PNG") || hasExt(".svg") || hasExt(".SVG")) {
    XcursorImage *img = ImageLoader::LoadAsXcursor(cursorPath, size, hotXPercent, hotYPercent);
    return LoadCursor(img);
  }

  // Fallback 1x1 transparent
  XcursorImage *tiny = XcursorImageCreate(1, 1);
  tiny->xhot = tiny->yhot = 0;
  tiny->pixels[0] = 0;
  return LoadCursor(tiny);
}

void X11CursorInjector::SetActive(bool active) {
  isActive_ = active;
  if (!isActive_) {
    RestoreAll();
  } else {
    lastLeaf_ = None;
    ApplyCursor();
  }
}

bool X11CursorInjector::IsActive() const { return isActive_; }

void X11CursorInjector::RestoreAll() {
  if (!display_)
    return;

  // Revoke the custom cursors across the system
  if (lastInjectedWindow_ != None && lastInjectedWindow_ != rootWindow_) {
    XUndefineCursor(display_, lastInjectedWindow_);
  }
  XUndefineCursor(display_, rootWindow_);
  XFlush(display_);
  lastLeaf_ = None;
  lastInjectedWindow_ = None;
}

bool X11CursorInjector::IsRunning() const { return true; }

} // namespace NeonSight
