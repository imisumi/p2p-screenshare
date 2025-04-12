#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>

class Renderer {
public:
    Renderer();
    ~Renderer();
    
    bool Initialize(HWND hWnd, int width, int height);
    void Shutdown();
    
    // Device and context access
    ID3D11Device* GetDevice() const { return m_Device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_Context.Get(); }

	Microsoft::WRL::ComPtr<ID3D11Device> GetDevicePtr() const { return m_Device; }
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> GetContextPtr() const { return m_Context; }
    
    // ImGui-specific methods
    bool InitImGui(HWND hWnd);
    void BeginImGuiFrame();
    void EndImGuiFrame();
    void ShutdownImGui();
    
    // Frame rendering
    void BeginFrame();
    void EndFrame();
    
    // Resize handling
    void Resize(uint32_t width, uint32_t height);
    
    // VSync control
    void SetVSync(bool enabled) { m_VSyncEnabled = enabled; }
    bool IsVSyncEnabled() const { return m_VSyncEnabled; }
    
    // Diagnostic methods
    void EnumerateAdapters();
    
    // Resource creation helpers
    bool CreateSamplerState(D3D11_FILTER filter, ID3D11SamplerState** ppSampler);
    
private:
    // Device creation and cleanup
    bool CreateDeviceAndSwapChain(HWND hWnd, int width, int height);
    bool CreateRenderTarget();
    void CleanupRenderTarget();
    
    // String helper
    std::string WStringToString(const std::wstring& wstr);
    
    // Output enumeration
    void EnumerateOutputs(IDXGIAdapter* pAdapter, UINT adapterIndex);
    
    // D3D11 resources
    Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
    
    // State
    bool m_VSyncEnabled = false;
    bool m_IsOccluded = false;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    
    // Clear color
    float m_ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};