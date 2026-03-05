#include "ImguiHelper.h"

namespace ImGuiHelper
{
    void TextWrappedColored(const ImVec4& col, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextWrappedV(fmt, args);
        ImGui::PopStyleColor();
        va_end(args);
    }

    std::string popupMessage;
    void QueuePopupMessage(std::string& message)
    {
        popupMessage = message; //copy string because "message" could be from the stack of what's calling it
        ImGui::OpenPopup(popupMessage.c_str());
    }

    void ProcessPopupMessage()
    {
        if (!popupMessage.empty())
        {
            //set popup size
            ImGui::SetNextWindowSize(ImVec2(0,0));

            //set popup at center
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal(popupMessage.c_str(), nullptr, ImGuiWindowFlags_NoTitleBar))
            {
                ImGui::TextUnformatted(popupMessage.c_str());
                if (ImGui::Button("Ok", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); popupMessage.clear(); }
                ImGui::EndPopup();
            }
        }
    }
}