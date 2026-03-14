#include "ClassCompare.h"
#include "ImguiHelper.h"
#include <tlhelp32.h>
#include <vector>

#define ADDRESS_WIDTH 150.0f

#define COLOR_SAME ImVec4(0.0f, 1.0f, 0.0f, 1.0f) //green
#define COLOR_DIFFERENT ImVec4(1.0f, 0.0f, 0.0f, 1.0f) //red
#define COLOR_LESS ImVec4(1.0f, 1.0f, 0.0f, 1.0f) //yellow
#define COLOR_GREATER ImVec4(0.5f, 0.5f, 1.0f, 1.0f) //blue

#define FLOAT_CMP(f1, f2) abs(f2-f1) <= 0.001 //compare floats with some leeway

typedef DWORD64 QWORD;

//class compare
HANDLE hProcess = INVALID_HANDLE_VALUE;
std::string WindowName;
char FocusAddress[20];
std::vector<std::string> Addresses;
int ClassSize = 0x800;
std::vector<BYTE> ClassInstanceMemory;
std::vector<std::vector<BYTE>> OtherClassesMemory;

//rendering
ID3D12GraphicsCommandList* CommandList;
ID3D12CommandQueue* CommandQueue;
ID3D12Fence* Fence;
HANDLE FenceEvent;
UINT64 FenceLastSignaledValue;
FrameContext frameContext[APP_NUM_FRAMES_IN_FLIGHT];
UINT FrameIndex;
HWND hwnd;
ID3D12Resource* MainRenderTargetResource[APP_NUM_BACK_BUFFERS];
ID3D12DescriptorHeap* SrvDescHeap;
IDXGISwapChain3* SwapChain;
bool SwapChainOccluded;
HANDLE SwapChainWaitableObject;
D3D12_CPU_DESCRIPTOR_HANDLE MainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS];

FrameContext* WaitForNextFrameContext()
{
    FrameContext* frame_context = &frameContext[FrameIndex % APP_NUM_FRAMES_IN_FLIGHT];
    if (Fence->GetCompletedValue() < frame_context->FenceValue)
    {
        Fence->SetEventOnCompletion(frame_context->FenceValue, FenceEvent);
        HANDLE waitableObjects[] = { SwapChainWaitableObject, FenceEvent };
        WaitForMultipleObjects(2, waitableObjects, TRUE, INFINITE);
    }
    else
        WaitForSingleObject(SwapChainWaitableObject, INFINITE);

    return frame_context;
}

void Render()
{
    ImGui::Render();

    FrameContext* frameCtx = WaitForNextFrameContext();
    const UINT backBufferIdx = SwapChain->GetCurrentBackBufferIndex();
    frameCtx->CommandAllocator->Reset();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = MainRenderTargetResource[backBufferIdx];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    CommandList->Reset(frameCtx->CommandAllocator, nullptr);
    CommandList->ResourceBarrier(1, &barrier);

    // Render Dear ImGui graphics
    const float clear_color_with_alpha[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    CommandList->ClearRenderTargetView(MainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
    CommandList->OMSetRenderTargets(1, &MainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
    CommandList->SetDescriptorHeaps(1, &SrvDescHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), CommandList);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    CommandList->ResourceBarrier(1, &barrier);
    CommandList->Close();

    CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);
    CommandQueue->Signal(Fence, ++FenceLastSignaledValue);
    frameCtx->FenceValue = FenceLastSignaledValue;

    // Present
    const HRESULT hr = SwapChain->Present(0, 0);
    SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    FrameIndex++;
}

BOOL CALLBACK EnumFunc(HWND hwnd, LPARAM lparam)
{
    char windowName[MAX_PATH]{};
    GetWindowTextA(hwnd, windowName, sizeof(windowName));
    if (windowName[0])
    {
        char processName[MAX_PATH + 2 + sizeof(QWORD)*2]{};
        sprintf_s(processName, "%s##%lld", windowName, (QWORD)hwnd); //hwnd is unique
        if (ImGui::Button(processName))
        {
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);
            hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
            if (!hProcess) hProcess = INVALID_HANDLE_VALUE; //invalid return value is -1 or 0 so setting it to always be -1 keeps everything simpler
            if (hProcess == INVALID_HANDLE_VALUE)
            {
                std::string message = "Could not get process handle\nError Code: " + std::to_string(GetLastError());
                ImGuiHelper::QueuePopupMessage(message);
                return FALSE; //returning false stops the enumeration
            }
            ZeroMemory(FocusAddress, sizeof(FocusAddress));
            OtherClassesMemory.clear();
            Addresses.clear();
            ClassInstanceMemory.clear();
            ClassInstanceMemory.resize(ClassSize);
            WindowName = windowName;
            ImGui::SetScrollY(0.0f);
        }
    }
    return TRUE;
}

void ClassCompareFrame()
{
    // Handle window screen locked
    if ((SwapChainOccluded && SwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || IsIconic(hwnd))
    {
        Sleep(10);
        return;
    }
    SwapChainOccluded = false;

    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    //fit to window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    ImGui::SetNextWindowSize(ImVec2((float)clientRect.right - (float)clientRect.left, (float)clientRect.bottom - (float)clientRect.top));

    //start imgui window
    ImGui::Begin("Class Compare", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (hProcess != INVALID_HANDLE_VALUE) //only show the button if a process is already selected
    {
        if (ImGui::Button("Select Different Process"))
        {
            CloseHandle(hProcess);
            hProcess = INVALID_HANDLE_VALUE;
        }
    }
    else
        ImGui::TextUnformatted("Select a process to attach to");

#if _DEBUG
    ImGui::Text("Current process handle: 0x%I64X", hProcess);
    ImGui::Text("FPS: %.0f", ImGui::GetIO().Framerate);
#endif

    if (hProcess == INVALID_HANDLE_VALUE)
    {
        EnumWindows(EnumFunc, NULL);
    }
    else
    {
        ImGui::Text("Selected Process: %s", WindowName.c_str());
        ImGui::NewLine();

        static bool extraCmp;
        ImGui::Checkbox("Include greater/less than comparisons", &extraCmp);
        static bool Hex;
        ImGui::Checkbox("Hex", &Hex);
        ImGui::SameLine();
        static bool Signed;
        ImGui::Checkbox("Signed", &Signed);
        ImGui::PushItemWidth(ADDRESS_WIDTH);
        if (ImGui::InputInt("Class size", &ClassSize, 0x100, 0x800, ImGuiInputTextFlags_CharsHexadecimal))
        {
            if (ClassSize <= 0) ClassSize = 0x800;
            ClassInstanceMemory.resize(ClassSize);
            for (auto& otherClassMemory : OtherClassesMemory) otherClassMemory.resize(ClassSize);
        }
        static Type type = Type_Int;
        static int byteSize = 4;
        if (ImGui::Combo("Value type", (int*)&type, "Byte\0Short\0Int\0Int64\0Float\0Double"))
        {
            switch (type)
            {
            case Type_Byte:
                byteSize = 1;
                break;
            case Type_Short:
                byteSize = 2;
                break;
            case Type_Int:
            case Type_Float:
                byteSize = 4;
                break;
            case Type_Int64:
            case Type_Double:
                byteSize = 8;
                break;
            }
        }
        ImGui::PopItemWidth();
        ImGui::NewLine();

        //get memory
        const QWORD base = FocusAddress[0] ? std::stoull(FocusAddress, nullptr, 16) : 0i64;
        const bool success = base > 0i64 ? ReadProcessMemory(hProcess, (LPCVOID)base, ClassInstanceMemory.data(), ClassInstanceMemory.size(), nullptr) : false;
        std::vector<bool> successes(Addresses.size());
        for (size_t i = 0; i < Addresses.size(); i++)
        {
            const QWORD base = Addresses[i][0] ? std::stoull(Addresses[i], nullptr, 16) : 0i64;
            successes[i] = base > 0i64 ? ReadProcessMemory(hProcess, (LPCVOID)base, OtherClassesMemory[i].data(), OtherClassesMemory[i].size(), nullptr) : false;
        }

        //for now, assume signed int type
        if (ImGui::BeginTable("Table", 3 + (int)Addresses.size(), ImGuiTableFlags_SizingFixedFit))
        {
            //enter addresses
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("       "); ImGui::TableNextColumn(); //leave space on the left for the offset display
            ImGui::PushItemWidth(ADDRESS_WIDTH); //this only works once per column
            ImGui::InputTextWithHint("##0", "Class address", FocusAddress, sizeof(void*)*2 + 1, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsUppercase);
            ImGui::PopItemWidth();
            for (size_t i = 0; i < Addresses.size(); i++)
            {
                char label[10]{};
                sprintf_s(label, "##%d", (int)(i + 1));
                ImGui::TableNextColumn();
                ImGui::PushItemWidth(ADDRESS_WIDTH);
                ImGui::InputTextWithHint(label, "Other class", (char*)Addresses[i].c_str(), sizeof(void*) * 2 + 1, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsUppercase);
                ImGui::PopItemWidth();
            }
            ImGui::TableNextColumn();
            if (ImGui::Button("+"))
            {
                Addresses.resize(Addresses.size() + 1);
                OtherClassesMemory.resize(Addresses.size());
                OtherClassesMemory.back().resize(ClassSize);
                successes.resize(Addresses.size());
            }
            if (Addresses.size())
            {
                ImGui::SameLine();
                if (ImGui::Button("-"))
                {
                    Addresses.pop_back();
                    OtherClassesMemory.pop_back();
                    successes.pop_back();
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Offset");

            for (int offset = 0x0; offset < ClassSize; offset += byteSize)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("0x%X  ", offset);
                ImGui::TableNextColumn();
                QWORD val1 = *(QWORD*)&ClassInstanceMemory[offset]; //store 8 bytes for simplicity
                char* format = nullptr;
                if (success)
                {
                    switch (type)
                    {
                    case Type_Byte:
                        if (Hex)
                            format = (char*)"0x%hhX";
                        else if (Signed)
                            format = (char*)"%hhd";
                        else
                            format = (char*)"%hhu";
                        break;
                    case Type_Short:
                        if (Hex)
                            format = (char*)"0x%hX";
                        else if (Signed)
                            format = (char*)"%hd";
                        else
                            format = (char*)"%hu";
                        break;
                    case Type_Int:
                        if (Hex)
                            format = (char*)"0x%X";
                        else if (Signed)
                            format = (char*)"%d";
                        else
                            format = (char*)"%u";
                        break;
                    case Type_Int64:
                        if (Hex)
                            format = (char*)"0x%I64X";
                        else if (Signed)
                            format = (char*)"%lld";
                        else
                            format = (char*)"%llu";
                        break;
                    case Type_Float:
                        if (Hex)
                            ImGui::TextWrapped("0x%X", val1);
                        else
                            ImGui::TextWrapped("%f", *(float*)&val1);
                        break;
                    case Type_Double:
                        if (Hex)
                            ImGui::TextWrapped("0x%I64X", val1);
                        else
                            ImGui::TextWrapped("%lf", *(double*)&val1);
                        break;
                    }

                    if (type <= Type_Int64)
                        ImGui::Text(format, val1);
                }
                for (size_t i = 0; i < Addresses.size(); i++)
                {
                    ImGui::TableNextColumn();
                    if (successes[i])
                    {
                        QWORD val2 = *(QWORD*)&OtherClassesMemory[i][offset];
                        ImVec4 Color;
                        bool isEqual;
                        bool isLess;
                        switch (type)
                        {
                        case Type_Byte:
                            isEqual = (val1 & 0xFF) == (val2 & 0xFF);
                            isLess = Signed ? (signed char)(val1 & 0xFF) > (signed char)(val2 & 0xFF) : (BYTE)(val1 & 0xFF) > (BYTE)(val2 & 0xFF);
                            break;
                        case Type_Short:
                            isEqual = (val1 & 0xFFFF) == (val2 & 0xFFFF);
                            isLess = Signed ? (signed short)(val1 & 0xFFFF) > (signed short)(val2 & 0xFFFF) : (WORD)(val1 & 0xFFFF) > (WORD)(val2 & 0xFFFF);
                            break;
                        case Type_Int:
                            isEqual = (val1 & 0xFFFFFFFF) == (val2 & 0xFFFFFFFF);
                            isLess = Signed ? (signed int)(val1 & 0xFFFFFFFF) > (signed int)(val2 & 0xFFFFFFFF) : (DWORD)(val1 & 0xFFFFFFFF) > (DWORD)(val2 & 0xFFFFFFFF);
                            break;
                        case Type_Int64:
                            isEqual = val1 == val2;
                            isLess = Signed ? (signed __int64)val1 > (signed __int64)val2 : val1 > val2;
                            break;
                        case Type_Float:
                            if (FLOAT_CMP(*(float*)&val1, *(float*)&val2))
                                Color = COLOR_SAME;
                            else
                            {
                                if (extraCmp)
                                {
                                    if (*(float*)&val2 < *(float*)&val1)
                                        Color = COLOR_LESS;
                                    else
                                        Color = COLOR_GREATER;
                                }
                                else
                                    Color = COLOR_DIFFERENT;
                            }
                            if (Hex)
                                ImGuiHelper::TextWrappedColored(Color, "0x%X", val2);
                            else
                                ImGuiHelper::TextWrappedColored(Color, "%f", *(float*)&val2);
                            break;
                        case Type_Double:
                            if (FLOAT_CMP(*(double*)&val1, *(double*)&val2))
                                Color = COLOR_SAME;
                            else
                            {
                                if (extraCmp)
                                {
                                    if (*(float*)&val2 < *(float*)&val1)
                                        Color = COLOR_LESS;
                                    else
                                        Color = COLOR_GREATER;
                                }
                                else
                                    Color = COLOR_DIFFERENT;
                            }
                            if (Hex)
                                ImGuiHelper::TextWrappedColored(Color, "0x%I64X", val2);
                            else
                                ImGuiHelper::TextWrappedColored(Color, "%lf", val2);
                            break;
                        }

                        if (type <= Type_Int64)
                        {
                            if (isEqual)
                                Color = COLOR_SAME;
                            else
                            {
                                if (extraCmp)
                                {
                                    if (isLess)
                                        Color = COLOR_LESS;
                                    else
                                        Color = COLOR_GREATER;
                                }
                                else
                                    Color = COLOR_DIFFERENT;
                            }
                            ImGui::TextColored(Color, format, val2);
                        }
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    //finish frame
    ImGuiHelper::ProcessPopupMessage();
    ImGui::End();
    Render();

}
