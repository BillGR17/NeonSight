#include "ConfigWindow.hpp"
#include "../utils/ImageLoader.hpp"
#include "../utils/UUIDv7.hpp"
#include "../config/AppConfig.hpp"
#include "../config/CursorMeta.hpp"
#include "../utils/Resources.hpp"
#include "ThemeManager.hpp"

#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <set>

namespace NeonSight {

ConfigWindow::ConfigWindow() = default;

ConfigWindow::~ConfigWindow() {
  for (auto &cursor : availableCursors_) {
    if (cursor.textureID) {
      glDeleteTextures(1, &cursor.textureID);
    }
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (window_) {
    glfwDestroyWindow(window_);
  }
  glfwTerminate();
}

std::string ConfigWindow::GetConfigPath() const {
  const char *home = std::getenv("HOME");
  std::string path = home ? std::string(home) : ".";
  return path + "/.config/NeonSight/cursors";
}

void ConfigWindow::Show() {
  hidden_ = false;
  if (window_)
    glfwShowWindow(window_);
}

void ConfigWindow::Hide() {
  hidden_ = true;
  if (window_)
    glfwHideWindow(window_);
}

bool ConfigWindow::Initialize() {
  if (!glfwInit()) {
    std::cerr << "ConfigWindow GLFW Init Error\n";
    return false;
  }

  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Start hidden

  window_ = glfwCreateWindow(800, 600, "NeonSight", nullptr, nullptr);
  if (!window_) {
    std::cerr << "ConfigWindow window creation Error\n";
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1); // Enable vsync

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr; 

  AppConfig::Get().Load();
  currentTheme_ = AppConfig::Get().theme;
  if (currentTheme_ == "day") {
    ThemeManager::ApplyDayTheme();
  } else {
    ThemeManager::ApplyStormTheme();
  }

  ImGui_ImplGlfw_InitForOpenGL(window_, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  fileDialog_.SetTitle("Select Custom Cursor (*.svg, *.png)");
  fileDialog_.SetTypeFilters({".svg", ".png"});

  AppConfig::Get().Load();
  cursorSize_ = AppConfig::Get().cursorSize;
  isCursorEnabled_ = AppConfig::Get().isCursorEnabled;
  currentSelection_ = AppConfig::Get().activeCursor;

  // Set Window Icon using embedded thumb
  int iconW, iconH;
  unsigned char *iconData = ImageLoader::LoadRawFromMemory(Thumb_data, Thumb_size, iconW, iconH, 64);
  if (iconData) {
    GLFWimage image;
    image.width = iconW;
    image.height = iconH;
    image.pixels = iconData;
    glfwSetWindowIcon(window_, 1, &image);
    ImageLoader::FreeRaw(iconData);
  }

  ScanCursorDirectory();

  return true;
}

void ConfigWindow::ScanCursorDirectory() {
  std::string dirPath = GetConfigPath();
  std::error_code ec;
  if (!std::filesystem::exists(dirPath, ec)) {
    std::filesystem::create_directories(dirPath, ec);
  }

  // Map filename to current asset to preserve textures
  std::vector<CursorAsset> oldAssets = std::move(availableCursors_);
  availableCursors_.clear();

  std::set<std::string> jsonFiles;
  std::set<std::string> imageStems;

  for (const auto &entry : std::filesystem::directory_iterator(dirPath)) {
    auto ext = entry.path().extension().string();
    std::string extLow = ext;
    std::transform(extLow.begin(), extLow.end(), extLow.begin(), ::tolower);

    if (extLow == ".json") {
      jsonFiles.insert(entry.path().stem().string());
    } else if (extLow == ".png" || extLow == ".svg") {
      std::string filename = entry.path().filename().string();
      
      // Try to find existing asset to reuse texture
      auto it = std::find_if(oldAssets.begin(), oldAssets.end(), [&](const CursorAsset& a) {
        return a.filename == filename;
      });

      if (it != oldAssets.end()) {
        availableCursors_.push_back(std::move(*it));
        oldAssets.erase(it);
        imageStems.insert(entry.path().stem().string());
      } else {
        CursorAsset asset;
        asset.filepath = entry.path().string();
        asset.filename = filename;
        asset.metaPath = (std::filesystem::path(dirPath) / (entry.path().stem().string() + ".json")).string();
        asset.textureID = ImageLoader::LoadAsTexture(asset.filepath, asset.width, asset.height);
        if (asset.textureID != 0) {
          asset.isConfigured = asset.meta.Load(asset.metaPath);
          imageStems.insert(entry.path().stem().string());
          availableCursors_.push_back(std::move(asset));
        }
      }
    }
  }

  // Clean up textures of removed assets
  for (auto &asset : oldAssets) {
    if (asset.textureID) glDeleteTextures(1, &asset.textureID);
  }

  for (const auto &stem : jsonFiles) {
    if (imageStems.find(stem) == imageStems.end()) {
      std::filesystem::path orphan = std::filesystem::path(dirPath) / (stem + ".json");
      std::filesystem::remove(orphan, ec);
    }
  }

  if (availableCursors_.empty()) {
    static const char *kDefaultSVG = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\" viewBox=\"0 0 32 32\">\n"
                                     "  <circle cx=\"16\" cy=\"16\" r=\"14\" fill=\"none\" stroke=\"black\" stroke-width=\"2\" />\n"
                                     "  <circle cx=\"16\" cy=\"16\" r=\"8\" fill=\"none\" stroke=\"black\" stroke-width=\"2\" />\n"
                                     "  <circle cx=\"16\" cy=\"16\" r=\"3\" fill=\"red\" />\n"
                                     "</svg>\n";
    std::string uuid = GenerateUUIDv7();
    std::filesystem::path imgDest = std::filesystem::path(dirPath) / (uuid + ".svg");
    std::filesystem::path metaDest = std::filesystem::path(dirPath) / (uuid + ".json");

    std::ofstream svgOut(imgDest);
    if (svgOut) {
      svgOut << kDefaultSVG;
      svgOut.close();
      CursorMeta defaultMeta;
      defaultMeta.hotspotX = 0.5f;
      defaultMeta.hotspotY = 0.5f;
      defaultMeta.Save(metaDest.string());
      ScanCursorDirectory();
    }
  }
}

void ConfigWindow::DeleteCursor(CursorAsset &asset) {
  std::error_code ec;
  if (asset.textureID) glDeleteTextures(1, &asset.textureID);
  std::filesystem::remove(asset.filepath, ec);
  if (!asset.metaPath.empty() && std::filesystem::exists(asset.metaPath)) {
    std::filesystem::remove(asset.metaPath, ec);
  }

  if (currentSelection_ == asset.filename) {
    currentSelection_.clear();
    AppConfig::Get().activeCursor.clear();
    AppConfig::Get().Save();
    if (onConfigChanged_) {
      onConfigChanged_("", cursorSize_, false, 0.5f, 0.5f);
    }
  }
}

bool ConfigWindow::Render() {
  if (shouldQuit_) return false;

  glfwPollEvents();
  if (glfwWindowShouldClose(window_)) {
    glfwSetWindowShouldClose(window_, GLFW_FALSE);
    showExitModal_ = true;
  }

  if (hidden_) return true;

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  int w, h;
  glfwGetFramebufferSize(window_, &w, &h);
  ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

  int pendingDelete = -1;
  static int confirmDeleteIdx = -1;

  if (ImGui::Begin("NeonSight Setup", nullptr, flags)) {
    bool configChanged = false;

    if (ImGui::Checkbox("Enable", &isCursorEnabled_)) configChanged = true;
    ImGui::SameLine();
    ImGui::PushItemWidth(200);
    ImGui::SliderInt("Size", &cursorSize_, 16, 512);
    if (ImGui::IsItemDeactivatedAfterEdit()) configChanged = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Set Cursor Hotspot") && !currentSelection_.empty()) {
      for (const auto &cursor : availableCursors_) {
        if (cursor.filename == currentSelection_) {
          showHotspotModal_ = true;
          modalCursorFile_ = cursor.filename;
          modalMetaPath_ = cursor.metaPath;
          modalTextureID_ = cursor.textureID;
          modalTextureW_ = cursor.width;
          modalTextureH_ = cursor.height;
          modalHotX_ = cursor.meta.hotspotX;
          modalHotY_ = cursor.meta.hotspotY;
          break;
        }
      }
    }

    ImGui::SameLine();
    bool isDark = (currentTheme_ == "storm");
    if (ImGui::Checkbox("Dark theme", &isDark)) {
      currentTheme_ = isDark ? "storm" : "day";
      if (isDark) ThemeManager::ApplyStormTheme();
      else ThemeManager::ApplyDayTheme();
      AppConfig::Get().theme = currentTheme_;
      AppConfig::Get().Save();
    }

    float uploadBtnW = ImGui::CalcTextSize("Upload Cursor").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - uploadBtnW - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button("Upload Cursor")) fileDialog_.Open();

    ImGui::Separator();

    if (availableCursors_.empty()) {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No cursors found.");
    } else {
      const float cellW = 108.0f;
      const float availW = ImGui::GetContentRegionAvail().x;
      const int numCols = std::max(1, (int)(availW / cellW));

      if (ImGui::BeginTable("cursor_grid", numCols, ImGuiTableFlags_NoPadOuterX)) {
        for (int i = 0; i < (int)availableCursors_.size(); ++i) {
          ImGui::TableNextColumn();
          const auto &cursor = availableCursors_[i];
          bool isSelected = (currentSelection_ == cursor.filename);

          if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));
          ImGui::PushID(i);
          if (ImGui::ImageButton(cursor.filename.c_str(), (void *)(intptr_t)cursor.textureID, ImVec2(100, 100))) {
            if (!cursor.isConfigured) {
              showHotspotModal_ = true;
              modalCursorFile_ = cursor.filename;
              modalMetaPath_ = cursor.metaPath;
              modalTextureID_ = cursor.textureID;
              modalTextureW_ = cursor.width;
              modalTextureH_ = cursor.height;
              modalHotX_ = 0.5f;
              modalHotY_ = 0.5f;
            } else {
              currentSelection_ = cursor.filename;
              hotspotX_ = cursor.meta.hotspotX;
              hotspotY_ = cursor.meta.hotspotY;
              configChanged = true;
            }
          }
          ImGui::PopID();
          if (isSelected) ImGui::PopStyleColor();

          ImGui::PushID(i + 10000);
          ImVec4 delBtnColor = (currentTheme_ == "day") ? ImVec4(0.961f, 0.165f, 0.396f, 1.0f) : ImVec4(0.969f, 0.463f, 0.557f, 1.0f);
          ImGui::PushStyleColor(ImGuiCol_Button, delBtnColor);
          float offX = (ImGui::GetColumnWidth() - 64.0f) * 0.5f;
          if (offX > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
          if (ImGui::Button("Delete", ImVec2(64.0f, 22.0f))) {
            confirmDeleteIdx = i;
            ImGui::OpenPopup("Delete Cursor?");
          }
          ImGui::PopStyleColor();
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
    }

    if (configChanged && onConfigChanged_) {
      AppConfig::Get().activeCursor = currentSelection_;
      AppConfig::Get().cursorSize = cursorSize_;
      AppConfig::Get().isCursorEnabled = isCursorEnabled_;
      AppConfig::Get().Save();

      std::string path;
      for (const auto &c : availableCursors_) {
        if (c.filename == currentSelection_) { path = c.filepath; break; }
      }
      onConfigChanged_(path, cursorSize_, isCursorEnabled_, hotspotX_, hotspotY_);
    }

    if (confirmDeleteIdx >= 0) ImGui::OpenPopup("Delete Cursor?");
    if (ImGui::BeginPopupModal("Delete Cursor?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to delete this cursor?");
        if (ImGui::Button("Delete", ImVec2(120, 0))) { pendingDelete = confirmDeleteIdx; confirmDeleteIdx = -1; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) { confirmDeleteIdx = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (showHotspotModal_) ImGui::OpenPopup("Set Cursor Hotspot");
    if (ImGui::BeginPopupModal("Set Cursor Hotspot", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
      ImGui::Text("Click image to set hotspot.");
      ImVec2 imgSize(std::min(400.0f, (float)modalTextureW_ * 2.0f), std::min(400.0f, (float)modalTextureH_ * 2.0f));
      ImVec2 imgPos = ImGui::GetCursorScreenPos();
      ImGui::Image((void *)(intptr_t)modalTextureID_, imgSize);
      if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        ImVec2 mp = ImGui::GetMousePos();
        modalHotX_ = (mp.x - imgPos.x) / imgSize.x;
        modalHotY_ = (mp.y - imgPos.y) / imgSize.y;
      }
      ImVec2 dot = ImVec2(imgPos.x + modalHotX_ * imgSize.x, imgPos.y + modalHotY_ * imgSize.y);
      ImGui::GetWindowDrawList()->AddCircleFilled(dot, 6.0f, IM_COL32(255, 0, 0, 255));
      if (ImGui::Button("Save", ImVec2(120, 0))) {
        CursorMeta meta; meta.hotspotX = modalHotX_; meta.hotspotY = modalHotY_; meta.Save(modalMetaPath_);
        std::string currentFilepath;
        auto it = std::find_if(availableCursors_.begin(), availableCursors_.end(), [&](const CursorAsset& c) {
          return c.filename == modalCursorFile_;
        });
        if (it != availableCursors_.end()) {
          it->meta = meta;
          it->isConfigured = true;
          currentFilepath = it->filepath;
        }
        currentSelection_ = modalCursorFile_; hotspotX_ = modalHotX_; hotspotY_ = modalHotY_;
        AppConfig::Get().activeCursor = currentSelection_; AppConfig::Get().Save();
        onConfigChanged_(currentFilepath, cursorSize_, isCursorEnabled_, hotspotX_, hotspotY_);
        showHotspotModal_ = false; ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0))) { showHotspotModal_ = false; ImGui::CloseCurrentPopup(); }
      ImGui::EndPopup();
    }

    if (showExitModal_) ImGui::OpenPopup("Exit Options");
    if (ImGui::BeginPopupModal("Exit Options", &showExitModal_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
      if (ImGui::Button("Minimize to Tray", ImVec2(180, 40))) { Hide(); showExitModal_ = false; ImGui::CloseCurrentPopup(); }
      ImGui::SameLine();
      if (ImGui::Button("Quit NeonSight", ImVec2(180, 40))) { shouldQuit_ = true; showExitModal_ = false; ImGui::CloseCurrentPopup(); }
      ImGui::EndPopup();
    }
  }
  ImGui::End();

  fileDialog_.Display();
  if (fileDialog_.HasSelected()) {
    std::filesystem::path sel = fileDialog_.GetSelected();
    std::string uuid = GenerateUUIDv7();
    std::filesystem::path dest = std::filesystem::path(GetConfigPath()) / (uuid + sel.extension().string());
    std::error_code ec; std::filesystem::copy(sel, dest, ec);
    if (!ec) { CursorMeta().Save((std::filesystem::path(GetConfigPath()) / (uuid + ".json")).string()); ScanCursorDirectory(); }
    fileDialog_.ClearSelected();
  }

  if (pendingDelete >= 0) { DeleteCursor(availableCursors_[pendingDelete]); availableCursors_.erase(availableCursors_.begin() + pendingDelete); }

  ImGui::Render();
  int display_w, display_h;
  glfwGetFramebufferSize(window_, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  glClearColor(currentTheme_ == "day" ? 0.9f : 0.15f, currentTheme_ == "day" ? 0.9f : 0.16f, currentTheme_ == "day" ? 0.9f : 0.23f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(window_);
  return true;
}

} // namespace NeonSight
