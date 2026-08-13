#pragma once

#include "imgui.h"
#include "render.hpp"  // Add this to access Render::glfwWindow
#include <GLFW/glfw3.h>
#include "list.hpp"
#include "search.hpp"
#include "core.hpp"

extern int selectedIndex;

namespace imGuiMenu {
// Remove inline fixed sizes
// inline float WIDTH = 160.f;
// inline float HEIGHT = 105.f;

extern std::atomic<int> tabCount;

inline float areaSeparatorSpace = 8.f;
inline float textSeparatorSpace = 4.f;
// Remove these fixed calculations
// inline float widthSeparatorInt = WIDTH / 2;
// inline float heightSeparatorInt = HEIGHT / 2 + 20;

inline ImFont* normalText;
inline ImFont* titleText;
inline ImFont* highlightText;
inline ImFont* espNameText;
inline ImFont* subTitleText;
inline ImFont* weaponIcons;

void menuBar();
void renderMenu(bool state);
void setStyle();

void verticalSplitter(float width, float height);
void horizontalSplitter(float height);

void process_tab_render();
void mem_search_render();
void miscRender();
void aboutMeRender();
}
