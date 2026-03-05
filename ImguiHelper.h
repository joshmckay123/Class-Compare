#pragma once
#include <string>
#include "imgui/imgui.h"

namespace ImGuiHelper
{
    void TextWrappedColored(const ImVec4& col, const char* fmt, ...);
    void QueuePopupMessage(std::string& message);
    void ProcessPopupMessage();
}