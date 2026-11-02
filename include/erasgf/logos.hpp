#pragma once
#include "erasgf/render.hpp"

namespace erasgf {
const std::vector<std::string>& logo_names();
std::vector<DrawOp> logo_mark(int choice, const Rect& bounds);
Scene launch_scene(int choice, const std::string& version, int width, int height);
} // namespace erasgf
