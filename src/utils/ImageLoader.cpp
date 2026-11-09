#include "ImageLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.hpp"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.hpp"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.hpp"

#include <iostream>
#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>
#include <GL/gl.h>

namespace NeonSight {

// Pixel buffer owned by value: RAII, no manual free needed by callers.
using RgbaBuffer = std::vector<unsigned char>;

// Returns std::nullopt when the image cannot be decoded.
using RgbaResult = std::optional<RgbaBuffer>;

static bool EndsWith(const std::string &str, const std::string &suffix) {
  if (str.length() < suffix.length())
    return false;
  return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

static void ResizeRGBA(const unsigned char *src, int sw, int sh, unsigned char *dst, int dw, int dh) {
  if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
    return;
  for (int y = 0; y < dh; y++) {
    float srcY = (float)y * (float)sh / (float)dh;
    int y0 = (int)srcY;
    int y1 = std::min(y0 + 1, sh - 1);
    float dy = srcY - y0;
    for (int x = 0; x < dw; x++) {
      float srcX = (float)x * (float)sw / (float)dw;
      int x0 = (int)srcX;
      int x1 = std::min(x0 + 1, sw - 1);
      float dx = srcX - x0;
      for (int c = 0; c < 4; c++) {
        float p00 = src[(y0 * sw + x0) * 4 + c];
        float p01 = src[(y0 * sw + x1) * 4 + c];
        float p10 = src[(y1 * sw + x0) * 4 + c];
        float p11 = src[(y1 * sw + x1) * 4 + c];
        float val = p00 * (1.0f - dx) * (1.0f - dy) + p01 * dx * (1.0f - dy) + p10 * (1.0f - dx) * dy + p11 * dx * dy;
        dst[(y * dw + x) * 4 + c] = (unsigned char)(val + 0.5f);
      }
    }
  }
}

// Render a source image into a square (requestedSize x requestedSize) RGBA buffer,
// transparent-padded and centered.
static RgbaResult LoadImageFile(const std::string &filepath, int &w, int &h, int &channels, int requestedSize) {
  RgbaBuffer data;

  auto fillCentered = [&](unsigned char *temp, int rw, int rh) {
    int offsetX = (w - rw) / 2;
    int offsetY = (h - rh) / 2;
    for (int y = 0; y < rh; ++y) {
      std::memcpy(data.data() + ((y + offsetY) * w + offsetX) * 4, temp + (y * rw * 4), (size_t)rw * 4);
    }
  };

  if (EndsWith(filepath, ".svg") || EndsWith(filepath, ".SVG")) {
    NSVGimage *image = nsvgParseFromFile(filepath.c_str(), "px", 96.0f);
    if (!image)
      return std::nullopt;

    float originalW = image->width;
    float originalH = image->height;
    if (originalW == 0 || originalH == 0) {
      originalW = (float)requestedSize;
      originalH = (float)requestedSize;
    }

    float scale = (float)requestedSize / std::max(originalW, originalH);
    int rw = (int)(originalW * scale);
    int rh = (int)(originalH * scale);
    if (rw <= 0) rw = 1;
    if (rh <= 0) rh = 1;

    w = h = requestedSize;
    data.assign((size_t)w * h * 4, 0);

    RgbaBuffer temp((size_t)rw * rh * 4);
    NSVGrasterizer *rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, image, 0, 0, scale, temp.data(), rw, rh, rw * 4);
    fillCentered(temp.data(), rw, rh);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    channels = 4;
    return data;
  }

  int sw, sh, sc;
  unsigned char *src = stbi_load(filepath.c_str(), &sw, &sh, &sc, 4);
  if (!src)
    return std::nullopt;

  float scale = (float)requestedSize / std::max(sw, sh);
  int rw = (int)(sw * scale);
  int rh = (int)(sh * scale);
  if (rw <= 0) rw = 1;
  if (rh <= 0) rh = 1;

  w = h = requestedSize;
  data.assign((size_t)w * h * 4, 0);

  RgbaBuffer temp((size_t)rw * rh * 4);
  ResizeRGBA(src, sw, sh, temp.data(), rw, rh);
  fillCentered(temp.data(), rw, rh);

  stbi_image_free(src);
  channels = 4;
  return data;
}

XcursorImage *ImageLoader::LoadAsXcursor(const std::string &filepath, int fallbackSize, float hotXPercent, float hotYPercent) {
  int w, h, channels;
  RgbaResult data = LoadImageFile(filepath, w, h, channels, fallbackSize);

  // If loading fails, generate the placeholder neon dot
  if (!data) {
    std::cerr << "Failed to load image: " << filepath << ". Generating placeholder." << std::endl;
    w = fallbackSize;
    h = fallbackSize;
  }

  XcursorImage *image = XcursorImageCreate(w, h);
  image->xhot = (int)(w * hotXPercent);
  image->yhot = (int)(h * hotYPercent);
  image->size = fallbackSize;

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      XcursorPixel pixel = 0;

      if (data) {
        // Xcursor expects ARGB 32-bit format (STB/nanosvg gives RGBA)
        const RgbaBuffer &buf = *data;
        int idx = (y * w + x) * 4;
        unsigned char r = buf[idx];
        unsigned char g = buf[idx + 1];
        unsigned char b = buf[idx + 2];
        unsigned char a = buf[idx + 3];

        int pr = (r * a) / 255;
        int pg = (g * a) / 255;
        int pb = (b * a) / 255;

        pixel = (a << 24) | (pr << 16) | (pg << 8) | pb;
      } else {
        // Generate Neon Placeholder
        int dx = x - w / 2;
        int dy = y - h / 2;
        int distSq = dx * dx + dy * dy;

        if ((distSq > 40 && distSq < 60) || (dx == 0 && std::abs(dy) > 10) || (dy == 0 && std::abs(dx) > 10)) {
          pixel = (255 << 24) | (255 << 16) | (0 << 8) | 255;
        } else if (distSq < 10) {
          pixel = (255 << 24) | (0 << 16) | (255 << 8) | 255;
        } else if (distSq > 60 && distSq < 80) {
          pixel = (255 << 24) | (0 << 16) | (0 << 8) | 0;
        }
      }

      image->pixels[y * w + x] = pixel;
    }
  }

  // `data` is a std::vector: freed automatically when it goes out of scope.
  return image;
}

GLuint ImageLoader::LoadAsTexture(const std::string &filepath, int &outWidth, int &outHeight) {
  int channels;
  RgbaResult data = LoadImageFile(filepath, outWidth, outHeight, channels, 128);
  if (!data)
    return 0;

  GLuint texID;
  glGenTextures(1, &texID);
  glBindTexture(GL_TEXTURE_2D, texID);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, outWidth, outHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data->data());

  return texID;
}

static RgbaResult LoadImageData(const unsigned char *inputData, unsigned int inputSize, int &w, int &h, int &channels, int requestedSize) {
  int sw, sh, sc;
  unsigned char *src = stbi_load_from_memory(inputData, (int)inputSize, &sw, &sh, &sc, 4);
  if (!src)
    return std::nullopt;

  float scale = (float)requestedSize / std::max(sw, sh);
  int rw = (int)(sw * scale);
  int rh = (int)(sh * scale);
  if (rw <= 0) rw = 1;
  if (rh <= 0) rh = 1;

  w = h = requestedSize;
  RgbaBuffer data((size_t)w * h * 4, 0);

  RgbaBuffer temp((size_t)rw * rh * 4);
  ResizeRGBA(src, sw, sh, temp.data(), rw, rh);

  // Center in square buffer
  int offsetX = (w - rw) / 2;
  int offsetY = (h - rh) / 2;
  for (int y = 0; y < rh; ++y) {
    std::memcpy(data.data() + ((y + offsetY) * w + offsetX) * 4, temp.data() + (y * rw * 4), (size_t)rw * 4);
  }

  stbi_image_free(src);
  channels = 4;
  return data;
}

std::optional<std::vector<unsigned char>> ImageLoader::LoadRaw(const std::string &filepath, int &outWidth, int &outHeight, int requestedSize) {
  int channels;
  return LoadImageFile(filepath, outWidth, outHeight, channels, requestedSize);
}

std::optional<std::vector<unsigned char>> ImageLoader::LoadRawFromMemory(const unsigned char *data, unsigned int size, int &outWidth, int &outHeight, int requestedSize) {
  int channels;
  return LoadImageData(data, size, outWidth, outHeight, channels, requestedSize);
}

} // namespace NeonSight
