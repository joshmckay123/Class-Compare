#pragma once
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include <dxgi1_5.h>

#define APP_NUM_FRAMES_IN_FLIGHT 2
#define APP_NUM_BACK_BUFFERS APP_NUM_FRAMES_IN_FLIGHT

enum Type
{
    Type_Byte,
    Type_Short,
    Type_Int,
    Type_Int64,
    Type_Float,
    Type_Double,
};

struct FrameContext
{
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                      FenceValue;
};

extern HANDLE hProcess;

extern ID3D12GraphicsCommandList* CommandList;
extern ID3D12CommandQueue* CommandQueue;
extern ID3D12Fence* Fence;
extern HANDLE FenceEvent;
extern UINT64 FenceLastSignaledValue;
extern FrameContext frameContext[APP_NUM_FRAMES_IN_FLIGHT];
extern UINT FrameIndex;
extern HWND hwnd;
extern ID3D12Resource* MainRenderTargetResource[APP_NUM_BACK_BUFFERS];
extern ID3D12DescriptorHeap* SrvDescHeap;
extern IDXGISwapChain3* SwapChain;
extern bool SwapChainOccluded;
extern HANDLE SwapChainWaitableObject;
extern D3D12_CPU_DESCRIPTOR_HANDLE MainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS];

void ClassCompareFrame();