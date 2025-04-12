// Window.cpp
#include "Window.h"

Window::Window(const std::wstring &title, int width, int height)
	: m_hWnd(nullptr), m_Title(title), m_Width(width), m_Height(height),
	  m_Running(false), m_CustomWndProc(nullptr)
{
	ZeroMemory(&m_WindowClass, sizeof(WNDCLASSEXW));
}

Window::~Window()
{
	LOG_INFO("Window destructor started");
	// Unregister window class
	if (m_WindowClass.hInstance)
	{
		UnregisterClassW(m_WindowClass.lpszClassName, m_WindowClass.hInstance);
	}
}

bool Window::Initialize()
{
	MY_ASSERT(m_CustomWndProc != nullptr, "Custom window procedure is not set");

	m_WindowClass = {
		sizeof(m_WindowClass),
		CS_CLASSDC,
		m_CustomWndProc,
		0L,
		0L,
		GetModuleHandle(nullptr),
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		L"ImGuiApp",
		nullptr};

	if (!::RegisterClassExW(&m_WindowClass))
	{
		LOG_ERROR("Failed to register window class");
		return false;
	}

	// Create window with sensible defaults
	RECT desktopRect;
	GetClientRect(GetDesktopWindow(), &desktopRect);
	int defaultWidth = std::min(2560, (int)(desktopRect.right * 0.8f));
	int defaultHeight = std::min(1440, (int)(desktopRect.bottom * 0.8f));

	m_hWnd = ::CreateWindowW(
		m_WindowClass.lpszClassName,
		L"MyApplication",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		defaultWidth,
		defaultHeight,
		nullptr,
		nullptr,
		m_WindowClass.hInstance,
		nullptr);

	if (!m_hWnd)
	{
		LOG_ERROR("Failed to create window");
		::UnregisterClassW(m_WindowClass.lpszClassName, m_WindowClass.hInstance);
		return false;
	}

	// Show the window
	::ShowWindow(m_hWnd, SW_SHOWDEFAULT);
	::UpdateWindow(m_hWnd);

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	// And use the Game Mode API if on Windows 10+
	typedef BOOL(WINAPI * PFN_SET_GAME_MODE)(BOOL);
	// HMODULE hGameMode = LoadLibrary(ws2s(L"GameMode.dll"));
	HMODULE hGameMode = LoadLibraryA("GameMode.dll");
	if (hGameMode)
	{
		PFN_SET_GAME_MODE pfnSetGameMode =
			(PFN_SET_GAME_MODE)GetProcAddress(hGameMode, "SetGameMode");
		if (pfnSetGameMode)
		{
			pfnSetGameMode(TRUE);
		}
		FreeLibrary(hGameMode);
	}

	m_Running = true;

	LOG_INFO("Window initialized successfully");

	return true;
}

void Window::ProcessMessages()
{
	MSG msg = {};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		if (msg.message == WM_QUIT)
		{
			m_Running = false;
		}
	}
}
