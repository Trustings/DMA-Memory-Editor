#pragma once

#include "imgui.h"
#include "render.hpp"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <atomic>

namespace imGuiMenu {

extern std::atomic<int> tabCount;

inline float areaSeparatorSpace = 8.f;
inline float textSeparatorSpace = 4.f;

inline ImFont* normalText    = nullptr;
inline ImFont* titleText     = nullptr;
inline ImFont* highlightText = nullptr;
inline ImFont* subTitleText  = nullptr;

void menuBar();
void renderMenu(bool state);
void setStyle();

void process_tab_render();
void mem_search_render();
void miscRender();
void aboutMeRender();

} // namespace imGuiMenu
