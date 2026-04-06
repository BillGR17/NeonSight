#pragma once

#include <string>
#include <vector>
#include <functional>

#include <imgui.h>
#include "imfilebrowser.hpp"
#include "../config/CursorMeta.hpp"

struct GLFWwindow;
typedef unsigned int GLuint;

namespace NeonSight {

struct CursorAsset {
  std::string filepath; // full path to the image file
  std::string filename; // basename  (e.g. "<uuid>.svg")
  std::string metaPath; // path to   "<uuid>.json"
  GLuint textureID{0};
  int width{0};
  int height{0};
  CursorMeta meta;          // loaded from the JSON sidecar
  bool isConfigured{false}; // whether meta.Load() succeeded
};

class ConfigWindow {
public:
  ConfigWindow();
  ~ConfigWindow();

  bool Initialize();

  // Updates UI. Returns false if the window was closed.
  bool Render();

  // Callback when a user changes config
  using OnConfigChangedCallback = std::function<void(const std::string &, int, bool, float, float)>;
  void SetOnConfigChangedCallback(OnConfigChangedCallback cb) { onConfigChanged_ = cb; }

  void Quit() { shouldQuit_ = true; }
  void Show();
  void Hide();
  bool IsVisible() const { return !hidden_; }

private:
  std::string GetConfigPath() const;
  void ScanCursorDirectory();
  void DeleteCursor(CursorAsset &asset);

  GLFWwindow *window_{nullptr};

  std::vector<CursorAsset> availableCursors_;
  std::string currentSelection_; // filename of selected cursor
  int cursorSize_{48};
  bool isCursorEnabled_{true};
  float hotspotX_{0.5f};
  float hotspotY_{0.5f};

  bool showHotspotModal_{false};
  std::string modalCursorFile_; // filename of cursor being configured
  std::string modalMetaPath_;   // sidecar path for the cursor being configured
  GLuint modalTextureID_{0};
  int modalTextureW_{0};
  int modalTextureH_{0};
  float modalHotX_{0.5f};
  float modalHotY_{0.5f};

  OnConfigChangedCallback onConfigChanged_;
  std::string currentTheme_{"storm"};

  bool showExitModal_{false};
  bool hidden_{false};
  bool shouldQuit_{false};

  ImGui::FileBrowser fileDialog_;
};

} // namespace NeonSight
