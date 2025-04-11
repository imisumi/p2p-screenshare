#include "Renderer.h"
#include "Log.h"
#include "MyAssert.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#include <sstream>

Renderer::Renderer()
{
    LOG_INFO("Renderer constructor started");
}

Renderer::~Renderer()
{
    Shutdown();
    LOG_INFO("Renderer destroyed");
}

bool Renderer::Initialize(HWND hWnd, int width, int height)
{
    MY_ASSERT(hWnd != nullptr, "Invalid window handle");
    
    m_Width = width;
    m_Height = height;
    
    // Create device and swap chain
    if (!CreateDeviceAndSwapChain(hWnd, width, height))
    {
        LOG_ERROR("Failed to create D3D device and swap chain");
        return false;
    }
    
    // Enumerate adapters for diagnostic info
    EnumerateAdapters();
    
    LOG_INFO("Renderer initialized successfully");
    return true;
}

void Renderer::Shutdown()
{
    LOG_INFO("Renderer shutting down");
    
    CleanupRenderTarget();
    
    if (m_SwapChain)
    {
        m_SwapChain->SetFullscreenState(FALSE, nullptr);
    }
    
    m_SwapChain.Reset();
    m_Context.Reset();
    m_Device.Reset();
}

bool Renderer::CreateDeviceAndSwapChain(HWND hWnd, int width, int height)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 2;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 240;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    // sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	// sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    // Create device
    UINT createDeviceFlags = 0;
#ifdef MYAPP_DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &sd,
        &m_SwapChain,
        &m_Device,
        &featureLevel,
        &m_Context);

    if (res == DXGI_ERROR_UNSUPPORTED)
    {
        LOG_WARN("Hardware device not supported, falling back to WARP device");
        // Try WARP device instead
        res = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createDeviceFlags,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &sd,
            &m_SwapChain,
            &m_Device,
            &featureLevel,
            &m_Context);
    }

    if (FAILED(res))
    {
        LOG_ERROR("Failed to create D3D11 device: {0:x}", res);
        return false;
    }

    // Log feature level
    switch (featureLevel)
    {
    case D3D_FEATURE_LEVEL_11_0:
        LOG_INFO("D3D Feature Level: 11.0");
        break;
    case D3D_FEATURE_LEVEL_10_1:
        LOG_INFO("D3D Feature Level: 10.1");
        break;
    case D3D_FEATURE_LEVEL_10_0:
        LOG_INFO("D3D Feature Level: 10.0");
        break;
    default:
        LOG_INFO("D3D Feature Level: Unknown");
        break;
    }

    // Create render target
    if (!CreateRenderTarget())
    {
        LOG_ERROR("Failed to create render target");
        return false;
    }

    return true;
}

bool Renderer::CreateRenderTarget()
{
    MY_ASSERT(m_SwapChain != nullptr, "Swap chain is null");
    MY_ASSERT(m_Device != nullptr, "Device is null");

    // Create a render target view
    Microsoft::WRL::ComPtr<ID3D11Texture2D> pBackBuffer;
    HRESULT hr = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr))
    {
        LOG_ERROR("Failed to get back buffer: {0:x}", hr);
        return false;
    }

    hr = m_Device->CreateRenderTargetView(pBackBuffer.Get(), nullptr, m_RenderTargetView.GetAddressOf());
    if (FAILED(hr))
    {
        LOG_ERROR("Failed to create render target view: {0:x}", hr);
        return false;
    }

    LOG_INFO("Render target created successfully");
    return true;
}

void Renderer::CleanupRenderTarget()
{
    m_RenderTargetView.Reset();
}

void Renderer::Resize(int width, int height)
{
    if (m_Device && m_SwapChain && width > 0 && height > 0)
    {
        LOG_INFO("Resizing swap chain: {0}x{1}", width, height);
        
        // Cache the new size
        m_Width = width;
        m_Height = height;
        
        // Release the render target
        CleanupRenderTarget();
        
        // Resize the swap chain
        HRESULT hr = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            LOG_ERROR("Failed to resize swap chain: {0:x}", hr);
            return;
        }
        
        // Recreate the render target
        if (!CreateRenderTarget())
        {
            LOG_ERROR("Failed to recreate render target after resize");
        }
    }
}

bool Renderer::CreateSamplerState(D3D11_FILTER filter, ID3D11SamplerState** ppSampler)
{
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = filter;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = m_Device->CreateSamplerState(&samplerDesc, ppSampler);
    if (FAILED(hr))
    {
        LOG_ERROR("Failed to create sampler state: {0:x}", hr);
        return false;
    }
    
    return true;
}

void Renderer::EnumerateAdapters()
{
    // Initialize DXGI factory
    Microsoft::WRL::ComPtr<IDXGIFactory> pFactory;
    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);

    if (FAILED(hr))
    {
        LOG_ERROR("Failed to create DXGI factory: {0:x}", hr);
        return;
    }

    // Enumerate adapters (graphics cards)
    Microsoft::WRL::ComPtr<IDXGIAdapter> pAdapter;
    for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC adapterDesc;
        pAdapter->GetDesc(&adapterDesc);

        std::wstringstream ss;
        ss << L"GPU " << i << L": " << adapterDesc.Description
           << L" (VRAM: " << adapterDesc.DedicatedVideoMemory / (1024 * 1024) << L" MB)";
        LOG_INFO(WStringToString(ss.str()));

        // Enumerate outputs (monitors) for this adapter
        EnumerateOutputs(pAdapter.Get(), i);
    }
}

void Renderer::EnumerateOutputs(IDXGIAdapter* pAdapter, UINT adapterIndex)
{
    if (!pAdapter)
        return;

    // Enumerate outputs (monitors) for this adapter
    Microsoft::WRL::ComPtr<IDXGIOutput> pOutput;
    for (UINT j = 0; pAdapter->EnumOutputs(j, &pOutput) != DXGI_ERROR_NOT_FOUND; ++j)
    {
        DXGI_OUTPUT_DESC outputDesc;
        pOutput->GetDesc(&outputDesc);

        // Get current display mode
        DEVMODEW devMode;
        ZeroMemory(&devMode, sizeof(devMode));
        devMode.dmSize = sizeof(devMode);

        if (EnumDisplaySettingsW(outputDesc.DeviceName, ENUM_CURRENT_SETTINGS, &devMode))
        {
            std::wstringstream ss;
            ss << L"  Monitor " << j << L": " << outputDesc.DeviceName
               << L" (" << devMode.dmPelsWidth << L"x" << devMode.dmPelsHeight
               << L" @" << devMode.dmDisplayFrequency << L"Hz)";
            LOG_INFO(WStringToString(ss.str()));
        }
    }
}

std::string Renderer::WStringToString(const std::wstring& wstr)
{
    // Simple wide string to string conversion
    std::string result;
    result.reserve(wstr.length());
    for (wchar_t c : wstr)
    {
        if (c <= 127)
        {
            result.push_back(static_cast<char>(c));
        }
        else
        {
            result.push_back('?');
        }
    }
    return result;
}

bool Renderer::InitImGui(HWND hWnd)
{
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Enable features
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // Setup style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer bindings
    if (!ImGui_ImplWin32_Init(hWnd))
    {
        LOG_ERROR("Failed to initialize ImGui Win32 backend");
        return false;
    }

    if (!ImGui_ImplDX11_Init(m_Device.Get(), m_Context.Get()))
    {
        LOG_ERROR("Failed to initialize ImGui DX11 backend");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    // Enable DPI awareness
    ImGui_ImplWin32_EnableDpiAwareness();

    LOG_INFO("ImGui initialized successfully");
    return true;
}

void Renderer::BeginImGuiFrame()
{
    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
	ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
}

void Renderer::EndImGuiFrame()
{
    // Render ImGui
    ImGui::Render();
    
    // Set render target
    m_Context->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), nullptr);
    
    // Render ImGui draw data
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    
    // Update and Render additional Platform Windows
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void Renderer::ShutdownImGui()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void Renderer::BeginFrame()
{
    // Clear the render target
    // m_Context->ClearRenderTargetView(m_RenderTargetView.Get(), m_ClearColor);
}

void Renderer::EndFrame()
{
    // Present with or without vsync
    HRESULT hr = m_SwapChain->Present(m_VSyncEnabled ? 1 : 0, 0);
    
    if (hr == DXGI_STATUS_OCCLUDED)
    {
        m_IsOccluded = true;
        LOG_INFO("Swap chain occluded");
    }
    else if (FAILED(hr))
    {
        LOG_ERROR("Present failed: {0:x}", hr);
        MY_ASSERT(false, "SwapChain Present failed");
    }
    else
    {
        m_IsOccluded = false;
    }
}