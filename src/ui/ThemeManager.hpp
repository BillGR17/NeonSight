#pragma once

#include <imgui.h>

namespace NeonSight {

class ThemeManager {
public:
  static void ApplyStormTheme() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    const ImVec4 bg = ImVec4(0.141f, 0.157f, 0.231f, 1.000f);      // #24283b
    const ImVec4 bg_dark = ImVec4(0.121f, 0.137f, 0.208f, 1.000f); // #1f2335
    const ImVec4 fg = ImVec4(0.753f, 0.792f, 0.961f, 1.000f);      // #c0caf5
    const ImVec4 blue = ImVec4(0.478f, 0.635f, 0.969f, 1.000f);    // #7aa2f7
    const ImVec4 blue2 = ImVec4(0.478f, 0.635f, 0.969f, 0.800f);
    const ImVec4 select = ImVec4(0.180f, 0.235f, 0.392f, 1.000f); // #2e3c64
    const ImVec4 border = ImVec4(0.066f, 0.074f, 0.110f, 0.500f); // #11131c
    const ImVec4 title = ImVec4(0.114f, 0.129f, 0.184f, 1.000f);  // #1d212f

    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 4.0f;

    colors[ImGuiCol_Text] = fg;
    colors[ImGuiCol_TextDisabled] = ImVec4(0.443f, 0.478f, 0.604f, 1.000f);
    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = bg_dark;
    colors[ImGuiCol_PopupBg] = bg_dark;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
    colors[ImGuiCol_FrameBg] = title;
    colors[ImGuiCol_FrameBgHovered] = select;
    colors[ImGuiCol_FrameBgActive] = select;
    colors[ImGuiCol_TitleBg] = title;
    colors[ImGuiCol_TitleBgActive] = title;
    colors[ImGuiCol_TitleBgCollapsed] = title;
    colors[ImGuiCol_MenuBarBg] = title;
    colors[ImGuiCol_ScrollbarBg] = bg_dark;
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.180f, 0.235f, 0.392f, 0.500f);
    colors[ImGuiCol_ScrollbarGrabHovered] = blue2;
    colors[ImGuiCol_ScrollbarGrabActive] = blue;
    colors[ImGuiCol_CheckMark] = blue;
    colors[ImGuiCol_SliderGrab] = blue;
    colors[ImGuiCol_SliderGrabActive] = blue;
    colors[ImGuiCol_Button] = title;
    colors[ImGuiCol_ButtonHovered] = select;
    colors[ImGuiCol_ButtonActive] = blue;
    colors[ImGuiCol_Header] = select;
    colors[ImGuiCol_HeaderHovered] = select;
    colors[ImGuiCol_HeaderActive] = blue;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = blue;
    colors[ImGuiCol_SeparatorActive] = blue;
    colors[ImGuiCol_ResizeGrip] = select;
    colors[ImGuiCol_ResizeGripHovered] = blue;
    colors[ImGuiCol_ResizeGripActive] = blue;
    colors[ImGuiCol_Tab] = title;
    colors[ImGuiCol_TabHovered] = select;
    colors[ImGuiCol_TabActive] = blue;
    colors[ImGuiCol_TabUnfocused] = title;
    colors[ImGuiCol_TabUnfocusedActive] = select;
    colors[ImGuiCol_PlotLines] = blue;
    colors[ImGuiCol_PlotLinesHovered] = blue;
    colors[ImGuiCol_PlotHistogram] = blue;
    colors[ImGuiCol_PlotHistogramHovered] = blue;
    colors[ImGuiCol_TextSelectedBg] = select;
    colors[ImGuiCol_DragDropTarget] = blue;
    colors[ImGuiCol_NavHighlight] = blue;
    colors[ImGuiCol_NavWindowingHighlight] = blue;
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800f, 0.800f, 0.800f, 0.200f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800f, 0.800f, 0.800f, 0.350f);
  }

  static void ApplyDayTheme() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    const ImVec4 bg = ImVec4(0.882f, 0.886f, 0.906f, 1.000f);      // #e1e2e7
    const ImVec4 bg_dark = ImVec4(0.816f, 0.820f, 0.835f, 1.000f); // #d0d1d6
    const ImVec4 fg = ImVec4(0.216f, 0.376f, 0.749f, 1.000f);      // #3760bf
    const ImVec4 blue = ImVec4(0.180f, 0.490f, 0.914f, 1.000f);    // #2e7de9
    const ImVec4 blue2 = ImVec4(0.180f, 0.490f, 0.914f, 0.800f);
    const ImVec4 select = ImVec4(0.706f, 0.710f, 0.725f, 1.000f); // #b4b5b9
    const ImVec4 border = ImVec4(0.600f, 0.600f, 0.640f, 0.500f); // #9999a3
    const ImVec4 title = ImVec4(0.776f, 0.784f, 0.824f, 1.000f);  // #c6c8d2

    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 4.0f;

    colors[ImGuiCol_Text] = fg;
    colors[ImGuiCol_TextDisabled] = ImVec4(0.518f, 0.549f, 0.710f, 1.000f);
    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = bg_dark;
    colors[ImGuiCol_PopupBg] = bg_dark;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(1.000f, 1.000f, 1.000f, 0.000f);
    colors[ImGuiCol_FrameBg] = title;
    colors[ImGuiCol_FrameBgHovered] = select;
    colors[ImGuiCol_FrameBgActive] = select;
    colors[ImGuiCol_TitleBg] = title;
    colors[ImGuiCol_TitleBgActive] = title;
    colors[ImGuiCol_TitleBgCollapsed] = title;
    colors[ImGuiCol_MenuBarBg] = title;
    colors[ImGuiCol_ScrollbarBg] = bg_dark;
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.706f, 0.710f, 0.725f, 0.500f);
    colors[ImGuiCol_ScrollbarGrabHovered] = blue2;
    colors[ImGuiCol_ScrollbarGrabActive] = blue;
    colors[ImGuiCol_CheckMark] = blue;
    colors[ImGuiCol_SliderGrab] = blue;
    colors[ImGuiCol_SliderGrabActive] = blue;
    colors[ImGuiCol_Button] = title;
    colors[ImGuiCol_ButtonHovered] = select;
    colors[ImGuiCol_ButtonActive] = blue;
    colors[ImGuiCol_Header] = select;
    colors[ImGuiCol_HeaderHovered] = select;
    colors[ImGuiCol_HeaderActive] = blue;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = blue;
    colors[ImGuiCol_SeparatorActive] = blue;
    colors[ImGuiCol_ResizeGrip] = select;
    colors[ImGuiCol_ResizeGripHovered] = blue;
    colors[ImGuiCol_ResizeGripActive] = blue;
    colors[ImGuiCol_Tab] = title;
    colors[ImGuiCol_TabHovered] = select;
    colors[ImGuiCol_TabActive] = blue;
    colors[ImGuiCol_TabUnfocused] = title;
    colors[ImGuiCol_TabUnfocusedActive] = select;
    colors[ImGuiCol_PlotLines] = blue;
    colors[ImGuiCol_PlotLinesHovered] = blue;
    colors[ImGuiCol_PlotHistogram] = blue;
    colors[ImGuiCol_PlotHistogramHovered] = blue;
    colors[ImGuiCol_TextSelectedBg] = select;
    colors[ImGuiCol_DragDropTarget] = blue;
    colors[ImGuiCol_NavHighlight] = blue;
    colors[ImGuiCol_NavWindowingHighlight] = blue;
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.200f, 0.200f, 0.200f, 0.200f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.200f, 0.200f, 0.200f, 0.350f);
  }
};

} // namespace NeonSight
