
#include "App.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <timeapi.h>
#include <d3d11.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#include <windows.h>
#include <iostream>
#include <ShellScalingApi.h>
#include "MyAssert.h"
#include "Codec/Decoder.h"

// Forward declaration
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

App *App::s_Instance = nullptr;

App &App::get()
{
	MY_ASSERT(s_Instance != nullptr, "Application instance is null!");
	return *s_Instance;
}

App::App()
{
	// Initialize logging system
	timeBeginPeriod(1);
	Log::Init();
	LOG_INFO("App constructor started");

	MY_ASSERT(s_Instance == nullptr, "Application instance already exists!");
	s_Instance = this;

	// Enumerate graphics devices for logging/debugging
	EnumerateAdapters();

	// Initialize window
	MY_ASSERT(InitWindow(), "Failed to initialize window");

	// Initialize Direct3D
	// MY_ASSERT(CreateDeviceD3D(m_Hwnd), "Failed to create D3D device");

	// // Initialize ImGui
	// MY_ASSERT(initImGui(), "Failed to initialize ImGui");

	m_Renderer = std::make_unique<Renderer>();
	m_Renderer->Initialize(m_Hwnd, 2560, 1440);
	m_Renderer->InitImGui(m_Hwnd);
	// m_Renderer->

	//? init encoder
	const char *argv[] = {"./build/Release/AppEncD3D11.exe", "-s", "3840x1440", "-codec",
						  "h264", "-preset", "p1", "-tuninginfo", "ultralowlatency",
						  "-bitrate", "50M", "-fps", "60", "-gop", "30"};
	// const char *argv[] = {"./build/Release/AppEncD3D11.exe", "-s", "3840x1440", "-codec",
	// 					  "h264", "-preset", "p1", "-tuninginfo", "ultralowlatency",
	// 					  "-bitrate", "5M", "-fps", "30"};
	// initEncoder(13, argv);
	NvEncoderInitParam encodeCLIOptions;
	int nWidth = 0, nHeight = 0, temp = 0;
	ParseCommandLine_AppEncD3D(15, argv, nWidth, nHeight, encodeCLIOptions, temp, true);

	m_Enc = std::make_unique<NvEncoderD3D11>(m_Renderer->GetDevice(), 3840, 2160, NV_ENC_BUFFER_FORMAT_ARGB);
	// m_Enc = std::make_unique<NvEncoderD3D11>(m_pd3dDevice.Get(), 3840, 2160, NV_ENC_BUFFER_FORMAT_ABGR);
	// NvEncoderD3D11 enc(m_DxDevice.Get(), m_Width, m_Height, NV_ENC_BUFFER_FORMAT_ARGB);
	InitializeEncoder(*m_Enc, encodeCLIOptions, NV_ENC_BUFFER_FORMAT_ARGB);

	m_EncodedTexture.Create(m_Renderer->GetDevice(), 3840, 2160, Texture2D::TextureType::DEFAULT, Texture2D::TextureFormat::BGRA8_UNORM);
	// m_EncodedTexture.Create(m_pd3dDevice.Get(), 3840, 2160, Texture2D::TextureType::DEFAULT, Texture2D::TextureFormat::RGBA8_UNORM);

	m_Decoder = std::make_unique<ScreenSharingDecoder>(m_Renderer->GetDevice(), m_Renderer->GetContext());

	cuInit(0);

	// Get CUDA device
	CUdevice cuDevice = 0;
	cuDeviceGet(&cuDevice, 0);

	// Create CUDA context
	CUcontext cuContext = nullptr;
	cuCtxCreate(&cuContext, CU_CTX_SCHED_BLOCKING_SYNC, cuDevice);
	bool success = m_NvDecoderDX11.Initialize(cuContext, m_Renderer->GetDevice(), m_Renderer->GetContext(),
											  cudaVideoCodec_H264, // Example codec, adjust as needed
											  3840, 2160);

	m_NewOutputTexture.CreateWithCustomFlags(
		m_Renderer->GetDevice(),
		3840,
		2160,
		D3D11_BIND_SHADER_RESOURCE,
		D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED,
		Texture2D::TextureFormat::BGR8);

	m_DesktopCapture = std::make_unique<DesktopCapture>(m_Renderer->GetDevice(), m_Renderer->GetContext());
	m_DesktopCapture->InitDesktopDuplication();

	LOG_INFO("App constructor completed successfully");
}

void App::EnumerateAdapters()
{
	// Initialize DXGI factory
	IDXGIFactory *pFactory = nullptr;
	HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&pFactory);

	MY_ASSERT(SUCCEEDED(hr), "Failed to create DXGI factory");
	if (FAILED(hr))
	{
		LOG_ERROR("Failed to create DXGI factory: {0:x}", hr);
		return;
	}

	// Enumerate adapters (graphics cards)
	IDXGIAdapter *pAdapter = nullptr;
	for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_ADAPTER_DESC adapterDesc;
		pAdapter->GetDesc(&adapterDesc);

		std::wstringstream ss;
		ss << L"GPU " << i << L": " << adapterDesc.Description
		   << L" (VRAM: " << adapterDesc.DedicatedVideoMemory / (1024 * 1024) << L" MB)";
		LOG_INFO(ws2s(ss.str()));

		// Enumerate outputs (monitors) for this adapter
		EnumerateOutputs(pAdapter, i);

		pAdapter->Release();
	}

	pFactory->Release();
}

void App::EnumerateOutputs(IDXGIAdapter *pAdapter, UINT adapterIndex)
{
	if (!pAdapter)
		return;

	// Enumerate outputs (monitors) for this adapter
	IDXGIOutput *pOutput = nullptr;
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
			LOG_INFO(ws2s(ss.str()));
		}

		pOutput->Release();
	}
}

std::string App::ws2s(const std::wstring &wstr)
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

bool App::InitWindow()
{
	m_Wc = {
		sizeof(m_Wc),
		CS_CLASSDC,
		WndProc,
		0L,
		0L,
		GetModuleHandle(nullptr),
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		L"ImGuiApp",
		nullptr};

	if (!::RegisterClassExW(&m_Wc))
	{
		LOG_ERROR("Failed to register window class");
		return false;
	}

	// Create window with sensible defaults
	RECT desktopRect;
	GetClientRect(GetDesktopWindow(), &desktopRect);
	int defaultWidth = std::min(2560, (int)(desktopRect.right * 0.8f));
	int defaultHeight = std::min(1440, (int)(desktopRect.bottom * 0.8f));

	m_Hwnd = ::CreateWindowW(
		m_Wc.lpszClassName,
		L"MyApplication",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		defaultWidth,
		defaultHeight,
		nullptr,
		nullptr,
		m_Wc.hInstance,
		nullptr);

	if (!m_Hwnd)
	{
		LOG_ERROR("Failed to create window");
		::UnregisterClassW(m_Wc.lpszClassName, m_Wc.hInstance);
		return false;
	}

	// Show the window
	::ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(m_Hwnd);

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

	LOG_INFO("Window initialized successfully");
	return true;
}




void App::run()
{
	LOG_INFO("Application starting main loop");
	m_Running = true;

	try
	{
		MSG msg = {};
		while (m_Running)
		{
			// Process messages
			onMessage();
			if (!m_Running)
				break;

			// Render frame
			{
				// ScopedTimer timer("App::PerFrame");
				PerFrame();
			}
			// std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	catch (const std::exception &e)
	{
		LOG_ERROR("Exception in main loop: {0}", e.what());
		MY_ASSERT(false, e.what());
	}

	LOG_INFO("Application exiting main loop");

	// Cleanup resources
	shutdown();
}

void App::PerFrame()
{
	// MY_ASSERT(m_Renderer->GetSwapChain() != nullptr, "SwapChain is null in PerFrame");

	// Check if window is minimized
	// if (m_SwapChainOccluded)
	// {
	// 	if (m_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
	// 	{
	// 		// Skip rendering if occluded
	// 		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	// 		return;
	// 	}
	// 	m_SwapChainOccluded = false;
	// }

	// Handle window resize
	if (m_ResizeWidth > 0 && m_ResizeHeight > 0)
	{
		LOG_INFO("Resizing swap chain: {0}x{1}", m_ResizeWidth, m_ResizeHeight);
		m_Renderer->Resize(m_ResizeWidth, m_ResizeHeight);
		m_ResizeWidth = m_ResizeHeight = 0;
	}

	// Update application state
	{
		// ScopedTimer timer("	App::onUpdate");
		onUpdate();
	}

	// Render the frame if not occluded
	// if (!m_SwapChainOccluded)
	{
		onImGuiRender();
	}

	// Present the frame with vsync
	m_Renderer->EndFrame();
}

std::vector<uint8_t> EncFrame(NvEncoderD3D11 &enc)
{
	static std::vector<uint8_t> out;
	out.clear();
	std::vector<NvEncOutputFrame> packets;
	enc.EncodeFrame(packets);

	for (const auto &packet : packets)
		out.insert(out.end(), packet.frame.begin(), packet.frame.end());

	return out;
}

void App::onUpdate()
{
	m_DesktopCapture->CaptureFrame();

	// Example: update delta time
	static float lastTime = 0.0f;
	float currentTime = GetTickCount() * 0.001f;
	m_DeltaTime = currentTime - lastTime;
	lastTime = currentTime;

	{
		// ScopedTimer timer("		App::encode");

		// Calculate time since last encode
		auto currentTime = std::chrono::high_resolution_clock::now();
		auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
							 currentTime - m_lastEncodeTime)
							 .count();

		// Only encode if enough time has passed
		if (elapsedMs >= m_targetFrameTimeMs)
		{
			const NvEncInputFrame *encoderInputFrame = m_Enc->GetNextInputFrame();
			ID3D11Texture2D *pTexBgra = reinterpret_cast<ID3D11Texture2D *>(encoderInputFrame->inputPtr);

			Texture2D::CopyTexture(m_Renderer->GetContext(), m_DesktopCapture->GetTexture(), pTexBgra);

			m_DecodedBuffer = EncFrame(*m_Enc);
			m_lastEncodeTime = currentTime;

			// ScopedTimer timer("		App::decode");
			if (!m_DecodedBuffer.empty())
			{
				// Decode the frame
				// successDecode = m_NvDecoderDX11.DecodeFrame(m_DecodedBuffer, outputTexture.Get());
				successDecode = m_NvDecoderDX11.DecodeFrame(m_DecodedBuffer, m_NewOutputTexture.GetTexture());
				if (!successDecode)
				{
					LOG_ERROR("Failed to decode frame");
					return;
				}
			}
		}
	}
}

void App::onImGuiRender()
{
	m_Renderer->BeginImGuiFrame();

	// Main application window
	{
		ImGui::Begin("Application");

		ImGui::Text("FPS: %.1f (%.3f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);

		if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
		}

		// Add your custom UI here
		if (ImGui::BeginTabBar("MyTabs"))
		{
			if (ImGui::BeginTabItem("Main"))
			{
				ImGui::Text("Application is running...");
				ImGui::Text("Window size: %dx%d", m_WindowWidth, m_WindowHeight);

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Debug"))
			{
				if (ImGui::Button("Test Assert"))
				{
					MY_ASSERT(false, "Test assert button pressed");
				}

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();


		{
			ImGui::Begin("DXGI Desktop Duplication");
			ImVec2 availableRegion = ImGui::GetContentRegionAvail();

			uint32_t textureWidth = m_DesktopCapture->GetWidth();
			uint32_t textureHeight = m_DesktopCapture->GetHeight();

			float aspectRatio = (float)textureWidth / (float)textureHeight;
			ImVec2 imageSize;
			if (availableRegion.x / aspectRatio <= availableRegion.y)
			{
				// Width constrained
				imageSize.x = availableRegion.x;
				imageSize.y = availableRegion.x / aspectRatio;
			}
			else
			{
				// Height constrained
				imageSize.y = availableRegion.y;
				imageSize.x = availableRegion.y * aspectRatio;
			}

			// Center the image in the available region
			ImVec2 cursorPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2(
				cursorPos.x + (availableRegion.x - imageSize.x) * 0.5f,
				cursorPos.y + (availableRegion.y - imageSize.y) * 0.5f));

			ImGui::Image((ImTextureID)m_DesktopCapture->GetTexture().GetShaderResourceView(), imageSize);

			ImGui::End();
		}

		// ImGui::End();
		{
			ImGui::Begin("decoded texture");
			ImVec2 availableRegion = ImGui::GetContentRegionAvail();

			uint32_t textureWidth = m_DesktopCapture->GetWidth();
			uint32_t textureHeight = m_DesktopCapture->GetHeight();

			float aspectRatio = (float)textureWidth / (float)textureHeight;
			ImVec2 imageSize;
			if (availableRegion.x / aspectRatio <= availableRegion.y)
			{
				// Width constrained
				imageSize.x = availableRegion.x;
				imageSize.y = availableRegion.x / aspectRatio;
			}
			else
			{
				// Height constrained
				imageSize.y = availableRegion.y;
				imageSize.x = availableRegion.y * aspectRatio;
			}

			// Center the image in the available region
			ImVec2 cursorPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2(
				cursorPos.x + (availableRegion.x - imageSize.x) * 0.5f,
				cursorPos.y + (availableRegion.y - imageSize.y) * 0.5f));
			// Add the tint color parameter (RGBA) with full alpha
			// ImGui::Image((ImTextureID)outputSRV.Get(), {1920, 1080}, ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1));
			// ImGui::Image(reinterpret_cast<ImTextureID>(outputSRV.Get()), imageSize);
			ImGui::Image(reinterpret_cast<ImTextureID>(m_NewOutputTexture.GetShaderResourceView()), imageSize);

			ImGui::End();
		}
	}

	m_Renderer->EndImGuiFrame();
}


void App::onMessage()
{
	// Process messages, but limit how many we handle per frame to stay responsive
	const int MAX_MESSAGES_PER_FRAME = 10;
	int messageCount = 0;

	MSG msg;
	while (messageCount < MAX_MESSAGES_PER_FRAME && ::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
	{
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);

		if (msg.message == WM_QUIT)
		{
			m_Running = false;
			return;
		}

		messageCount++;
	}
}

void App::shutdown()
{
	LOG_INFO("Application shutting down");

	m_Running = false;

	// if (m_pPointSampler)
	// {
	// 	m_pPointSampler->Release();
	// 	m_pPointSampler = nullptr;
	// }


	// Destroy window
	if (m_Hwnd)
	{
		::DestroyWindow(m_Hwnd);
		m_Hwnd = nullptr;
	}

	// Unregister window class
	if (m_Wc.hInstance)
	{
		::UnregisterClassW(m_Wc.lpszClassName, m_Wc.hInstance);
		m_Wc = {};
	}

	LOG_INFO("Application shutdown complete");
	s_Instance = nullptr;
}

void App::resize(UINT width, UINT height)
{
	if (width > 0 && height > 0)
	{
		m_WindowWidth = width;
		m_WindowHeight = height;
		m_ResizeWidth = width;
		m_ResizeHeight = height;
		LOG_INFO("Window resize requested: {0}x{1}", width, height);
	}
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Static WndProc implementation
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	App &app = App::get();
	static bool inSystemMenu = false;

	switch (msg)
	{
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED)
		{
			app.resize(LOWORD(lParam), HIWORD(lParam));
		}
		return 0;

	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_MOUSEMENU || (wParam & 0xFFF0) == SC_KEYMENU)
		{
			if (!inSystemMenu)
			{
				inSystemMenu = true;
				::SetTimer(hWnd, 1, 1, NULL);
				LOG_INFO("Entering system menu");
			}
		}
		else if (wParam == SC_CLOSE)
		{
			LOG_INFO("WM_SYSCOMMAND: SC_CLOSE");
		}
		break;

	case WM_ENTERMENULOOP:
		inSystemMenu = true;
		::SetTimer(hWnd, 1, 1, NULL);
		LOG_INFO("Entering menu loop");
		break;

	case WM_EXITMENULOOP:
		inSystemMenu = false;
		::KillTimer(hWnd, 1);
		LOG_INFO("Exiting menu loop");
		break;

	case WM_ENTERSIZEMOVE:
		// app.DisableVSync();
		::SetTimer(hWnd, 1, 0, NULL);
		LOG_INFO("Entering size/move modal loop");
		break;

	case WM_EXITSIZEMOVE:
		// app.EnableVSync();
		::KillTimer(hWnd, 1);
		LOG_INFO("Exiting size/move modal loop");
		break;
	case WM_TIMER:
		if (wParam == 1)
		{
			// Process messages but don't let them block rendering
			MSG msg;
			while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					::PostQuitMessage(0);
					return 0;
				}

				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}

			// Force render with no vsync during modal operations
			app.PerFrame();
			return 0;
		}
		break;
		// case WM_TIMER:
		// 	if (wParam == 1)
		// 	{
		// 		// Process frames during modal states
		// 		app.PerFrame();
		// 	}
		// 	break;
		// return 0;

	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;

	case WM_DPICHANGED:
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)
		{
			const RECT *suggested_rect = (RECT *)lParam;
			::SetWindowPos(hWnd, nullptr,
						   suggested_rect->left, suggested_rect->top,
						   suggested_rect->right - suggested_rect->left,
						   suggested_rect->bottom - suggested_rect->top,
						   SWP_NOZORDER | SWP_NOACTIVATE);
		}
		break;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

#if 0
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	App &app = App::get();

	// Add this flag to track menu state
	static bool inSystemMenu = false;

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		app.resize((UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
		return 0;
	case WM_SYSCOMMAND:
		std::cout << "WM_SYSCOMMAND: " << std::hex << wParam << std::dec << std::endl;

		// Check specifically for system menu activation
		if ((wParam & 0xFFF0) == SC_MOUSEMENU || (wParam & 0xFFF0) == SC_KEYMENU)
		{
			// System menu is being activated via mouse or keyboard
			if (!inSystemMenu)
			{
				inSystemMenu = true;
				::SetTimer(hWnd, 1, 1, NULL);
				std::cout << "Entering system menu" << std::endl;
			}
		}
		break;
	case WM_NCRBUTTONUP:
		// This often happens just before the system menu appears
		std::cout << "WM_NCRBUTTONUP (possibly opening system menu)" << std::endl;
		if (!inSystemMenu)
		{
			inSystemMenu = true;
			::SetTimer(hWnd, 1, 1, NULL);
			std::cout << "Potentially entering system menu" << std::endl;
		}
		break;
	case WM_COMMAND:
	case WM_CANCELMODE:
	case WM_CAPTURECHANGED:
		// These can signal the end of a menu operation
		if (inSystemMenu)
		{
			inSystemMenu = false;
			::KillTimer(hWnd, 1);
			std::cout << "Potentially exiting system menu" << std::endl;
		}
		break;
	case WM_ENTERSIZEMOVE:
		::SetTimer(hWnd, 1, 1, NULL);
		std::cout << "Entering size/move modal loop" << std::endl;
		break;
	case WM_EXITSIZEMOVE:
		::KillTimer(hWnd, 1);
		std::cout << "Exiting size/move modal loop" << std::endl;
		break;
	case WM_ENTERMENULOOP:
		inSystemMenu = true;
		::SetTimer(hWnd, 1, 1, NULL);
		std::cout << "Entering menu loop" << std::endl;
		break;
	case WM_EXITMENULOOP:
		inSystemMenu = false;
		::KillTimer(hWnd, 1);
		std::cout << "Exiting menu loop" << std::endl;
		break;
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
		// Mouse button up outside the menu might close it
		if (inSystemMenu)
		{
			// Check if we should exit system menu state
			POINT pt;
			GetCursorPos(&pt);
			RECT rcWindow;
			GetWindowRect(hWnd, &rcWindow);

			// If click is outside window bounds, probably menu closed
			if (pt.x < rcWindow.left || pt.x > rcWindow.right ||
				pt.y < rcWindow.top || pt.y > rcWindow.bottom)
			{
				inSystemMenu = false;
				::KillTimer(hWnd, 1);
				std::cout << "Mouse click outside window - exiting system menu" << std::endl;
			}
		}
		break;
	case WM_TIMER:
		if (wParam == 1)
		{
			// This is our modal operation timer - perform necessary updates here
			app.PerFrame();
		}
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	default:
		// You can comment this out if it produces too much log output
		// std::cout << "message: " << msg << std::endl;
		break;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
#endif