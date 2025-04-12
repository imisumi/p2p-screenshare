// Window.h
#pragma once

#include <windows.h>
#include <string>
#include <functional>

#include "Utils.h"

class Window {
public:
    Window(const std::wstring& title = L"Application", int width = 1280, int height = 800);
    ~Window();

    // Initialize the window
    bool Initialize();
    
    // Process window messages
    void ProcessMessages();
    
    // Getters
    HWND GetHandle() const { return m_hWnd; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    bool IsRunning() const { return m_Running; }
    
    // Set custom window procedure
    void SetCustomWndProc(WNDPROC wndProc) { m_CustomWndProc = wndProc; }
    
    // Resize notification
    void Resize(uint32_t width, uint32_t height) { m_Width = width; m_Height = height; }

private:
    // Window procedure    
    HWND m_hWnd = nullptr;
    WNDCLASSEXW m_WindowClass =  {};
    std::wstring m_Title;
    uint32_t m_Width;
    uint32_t m_Height;
    bool m_Running;
    
    // Custom window procedure function pointer
    WNDPROC m_CustomWndProc = nullptr;
};