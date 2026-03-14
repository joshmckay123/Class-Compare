//credit to Omar Cornut for ImGui

#include "ClassCompare.h"
#include "resource.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

#define APP_SRV_HEAP_SIZE 64

struct ExampleDescriptorHeapAllocator
{
    ID3D12DescriptorHeap* Heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu{};
    UINT                        HeapHandleIncrement;
    ImVector<int>               FreeIndices;

    void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
    {
        IM_ASSERT(Heap == nullptr && FreeIndices.empty());
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapType = desc.Type;
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for (int n = desc.NumDescriptors; n > 0; n--)
            FreeIndices.push_back(n - 1);
    }
    void Destroy()
    {
        Heap = nullptr;
        FreeIndices.clear();
    }
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
    {
        IM_ASSERT(FreeIndices.Size > 0);
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
    {
        int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        IM_ASSERT(cpu_idx == gpu_idx);
        FreeIndices.push_back(cpu_idx);
    }
};

ID3D12Device* Device = nullptr;
ID3D12DescriptorHeap* RtvDescHeap;
ExampleDescriptorHeapAllocator SrvDescHeapAlloc{};
bool SwapChainTearingSupport;

void WaitForPendingOperations()
{
    CommandQueue->Signal(Fence, ++FenceLastSignaledValue);
    Fence->SetEventOnCompletion(FenceLastSignaledValue, FenceEvent);
    WaitForSingleObject(FenceEvent, INFINITE);
}

void CleanupRenderTarget()
{
    WaitForPendingOperations();

    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
        if (MainRenderTargetResource[i]) { MainRenderTargetResource[i]->Release(); MainRenderTargetResource[i] = nullptr; }
}

void CreateRenderTarget()
{
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource* pBackBuffer = nullptr;
        SwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
        Device->CreateRenderTargetView(pBackBuffer, nullptr, MainRenderTargetDescriptor[i]);
        MainRenderTargetResource[i] = pBackBuffer;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (Device && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            SwapChain->GetDesc1(&desc);
            HRESULT result = SwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), desc.Format, desc.Flags);
            IM_ASSERT(SUCCEEDED(result) && "Failed to resize swapchain.");
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = APP_NUM_BACK_BUFFERS;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;
    }

    // Create device
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&Device)) != S_OK)
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = APP_NUM_BACK_BUFFERS;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 1;
    if (Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&RtvDescHeap)) != S_OK)
        return false;

    SIZE_T rtvDescriptorSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = RtvDescHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
    {
        MainRenderTargetDescriptor[i] = rtvHandle;
        rtvHandle.ptr += rtvDescriptorSize;
    }

    D3D12_DESCRIPTOR_HEAP_DESC descriptorDesc = {};
    descriptorDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorDesc.NumDescriptors = APP_SRV_HEAP_SIZE;
    descriptorDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (Device->CreateDescriptorHeap(&descriptorDesc, IID_PPV_ARGS(&SrvDescHeap)) != S_OK)
        return false;
    SrvDescHeapAlloc.Create(Device, SrvDescHeap);

    D3D12_COMMAND_QUEUE_DESC commandDesc = {};
    commandDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    commandDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    commandDesc.NodeMask = 1;
    if (Device->CreateCommandQueue(&commandDesc, IID_PPV_ARGS(&CommandQueue)) != S_OK)
        return false;

    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frameContext[i].CommandAllocator)) != S_OK)
            return false;

    if (Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&CommandList)) != S_OK ||
        CommandList->Close() != S_OK)
        return false;

    if (Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)) != S_OK)
        return false;

    FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!FenceEvent)
        return false;

    IDXGIFactory5* dxgiFactory = nullptr;
    IDXGISwapChain1* swapChain1 = nullptr;
    if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
        return false;

    BOOL allow_tearing = FALSE;
    dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing));
    SwapChainTearingSupport = (allow_tearing == TRUE);
    if (SwapChainTearingSupport)
        sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    if (dxgiFactory->CreateSwapChainForHwnd(CommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
        return false;
    if (swapChain1->QueryInterface(IID_PPV_ARGS(&SwapChain)) != S_OK)
        return false;
    if (SwapChainTearingSupport)
        dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

    swapChain1->Release();
    dxgiFactory->Release();
    SwapChain->SetMaximumFrameLatency(APP_NUM_BACK_BUFFERS);
    SwapChainWaitableObject = SwapChain->GetFrameLatencyWaitableObject();

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (SwapChain) { SwapChain->SetFullscreenState(false, nullptr); SwapChain->Release(); SwapChain = nullptr; }
    if (SwapChainWaitableObject != nullptr) { CloseHandle(SwapChainWaitableObject); }
    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (frameContext[i].CommandAllocator) { frameContext[i].CommandAllocator->Release(); frameContext[i].CommandAllocator = nullptr; }
    if (CommandQueue) { CommandQueue->Release(); CommandQueue = nullptr; }
    if (CommandList) { CommandList->Release(); CommandList = nullptr; }
    if (RtvDescHeap) { RtvDescHeap->Release(); RtvDescHeap = nullptr; }
    if (SrvDescHeap) { SrvDescHeap->Release(); SrvDescHeap = nullptr; }
    if (Fence) { Fence->Release(); Fence = nullptr; }
    if (FenceEvent) { CloseHandle(FenceEvent); FenceEvent = nullptr; }
    if (Device) { Device->Release(); Device = nullptr; }
}

int main()
{
	// Make process DPI aware and obtain main monitor scale
	ImGui_ImplWin32_EnableDpiAwareness();
	const float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	// Create application window
	const WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_ICON1)), nullptr, nullptr, nullptr, L"Class Compare", LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_ICON1)) };
	RegisterClassExW(&wc);
	hwnd = CreateWindow(wc.lpszClassName, L"Class Compare", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; //Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale); //Bake a fixed style scale
    style.FontScaleDpi = main_scale; //Set initial font scale

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = Device;
    init_info.CommandQueue = CommandQueue;
    init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = SrvDescHeap;
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) { return SrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle); };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) { return SrvDescHeapAlloc.Free(cpu_handle, gpu_handle); };
    ImGui_ImplDX12_Init(&init_info);

    // Main message loop:
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT)
            break;

        //application logic
        ClassCompareFrame();
    }

    WaitForPendingOperations();

    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    CloseHandle(hProcess);

	return 0;
}
