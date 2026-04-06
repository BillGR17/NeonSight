#include "ImageLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.hpp"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.hpp"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.hpp"

#include <iostream>
#include <algorithm>
#include <GL/gl.h>

namespace NeonSight {

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

static unsigned char *LoadImageFile(const std::string &filepath, int &w, int &h, int &channels, int requestedSize) {
  if (EndsWith(filepath, ".svg") || EndsWith(filepath, ".SVG")) {
    NSVGimage *image = nsvgParseFromFile(filepath.c_str(), "px", 96.0f);
    if (!image)
      return nullptr;
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
    unsigned char *data = (unsigned char *)calloc(1, w * h * 4);
    
    unsigned char *temp = (unsigned char *)malloc(rw * rh * 4);
    NSVGrasterizer *rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, image, 0, 0, scale, temp, rw, rh, rw * 4);
    
    // Center in square buffer
    int offsetX = (w - rw) / 2;
    int offsetY = (h - rh) / 2;
    for (int y = 0; y < rh; ++y) {
      memcpy(data + ((y + offsetY) * w + offsetX) * 4, temp + (y * rw * 4), rw * 4);
    }

    free(temp);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    channels = 4;
    return data;
  } else {
    int sw, sh, sc;
    unsigned char *src = stbi_load(filepath.c_str(), &sw, &sh, &sc, 4);
    if (!src)
      return nullptr;

    float scale = (float)requestedSize / std::max(sw, sh);
    int rw = (int)(sw * scale);
    int rh = (int)(sh * scale);
    if (rw <= 0) rw = 1;
    if (rh <= 0) rh = 1;

    w = h = requestedSize;
    unsigned char *data = (unsigned char *)calloc(1, w * h * 4);
    
    unsigned char *temp = (unsigned char *)malloc(rw * rh * 4);
    ResizeRGBA(src, sw, sh, temp, rw, rh);

    // Center in square buffer
    int offsetX = (w - rw) / 2;
    int offsetY = (h - rh) / 2;
    for (int y = 0; y < rh; ++y) {
      memcpy(data + ((y + offsetY) * w + offsetX) * 4, temp + (y * rw * 4), rw * 4);
    }

    free(temp);
    stbi_image_free(src);
    channels = 4;
    return data;
  }
}

static void FreeImageFile(const std::string &/*filepath*/, unsigned char *data) {
  if (data)
    free(data);
}

XcursorImage *ImageLoader::LoadAsXcursor(const std::string &filepath, int fallbackSize, float hotXPercent, float hotYPercent) {
  int w, h, channels;
  unsigned char *data = LoadImageFile(filepath, w, h, channels, fallbackSize);

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
        int idx = (y * w + x) * 4;
        unsigned char r = data[idx];
        unsigned char g = data[idx + 1];
        unsigned char b = data[idx + 2];
        unsigned char a = data[idx + 3];

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

  FreeImageFile(filepath, data);

  return image;
}

GLuint ImageLoader::LoadAsTexture(const std::string &filepath, int &outWidth, int &outHeight) {
  int channels;
  unsigned char *data = LoadImageFile(filepath, outWidth, outHeight, channels, 128);
  if (!data)
    return 0;

  GLuint texID;
  glGenTextures(1, &texID);
  glBindTexture(GL_TEXTURE_2D, texID);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, outWidth, outHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

  FreeImageFile(filepath, data);
  return texID;
}

static unsigned char *LoadImageData(const unsigned char *inputData, unsigned int inputSize, int &w, int &h, int &channels, int requestedSize) {
  int sw, sh, sc;
  unsigned char *src = stbi_load_from_memory(inputData, (int)inputSize, &sw, &sh, &sc, 4);
  if (!src)
    return nullptr;

  float scale = (float)requestedSize / std::max(sw, sh);
  int rw = (int)(sw * scale);
  int rh = (int)(sh * scale);
  if (rw <= 0) rw = 1;
  if (rh <= 0) rh = 1;

  w = h = requestedSize;
  unsigned char *data = (unsigned char *)calloc(1, w * h * 4);
  
  unsigned char *temp = (unsigned char *)malloc(rw * rh * 4);
  ResizeRGBA(src, sw, sh, temp, rw, rh);

  // Center in square buffer
  int offsetX = (w - rw) / 2;
  int offsetY = (h - rh) / 2;
  for (int y = 0; y < rh; ++y) {
    memcpy(data + ((y + offsetY) * w + offsetX) * 4, temp + (y * rw * 4), rw * 4);
  }

  free(temp);
  stbi_image_free(src);
  channels = 4;
  return data;
}

unsigned char *ImageLoader::LoadRaw(const std::string &filepath, int &outWidth, int &outHeight, int requestedSize) {
  int channels;
  return LoadImageFile(filepath, outWidth, outHeight, channels, requestedSize);
}

unsigned char *ImageLoader::LoadRawFromMemory(const unsigned char *data, unsigned int size, int &outWidth, int &outHeight, int requestedSize) {
  int channels;
  return LoadImageData(data, size, outWidth, outHeight, channels, requestedSize);
}

void ImageLoader::FreeRaw(unsigned char *data) {
  if (data) free(data);
}

} // namespace NeonSight
