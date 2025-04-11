
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
	MY_ASSERT(CreateDeviceD3D(m_Hwnd), "Failed to create D3D device");

	// Initialize ImGui
	MY_ASSERT(initImGui(), "Failed to initialize ImGui");

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

	m_Enc = std::make_unique<NvEncoderD3D11>(m_pd3dDevice.Get(), 3840, 2160, NV_ENC_BUFFER_FORMAT_ARGB);
	// m_Enc = std::make_unique<NvEncoderD3D11>(m_pd3dDevice.Get(), 3840, 2160, NV_ENC_BUFFER_FORMAT_ABGR);
	// NvEncoderD3D11 enc(m_DxDevice.Get(), m_Width, m_Height, NV_ENC_BUFFER_FORMAT_ARGB);
	InitializeEncoder(*m_Enc, encodeCLIOptions, NV_ENC_BUFFER_FORMAT_ARGB);

	m_EncodedTexture.Create(m_pd3dDevice.Get(), 3840, 2160, Texture2D::TextureType::DEFAULT, Texture2D::TextureFormat::BGRA8_UNORM);
	// m_EncodedTexture.Create(m_pd3dDevice.Get(), 3840, 2160, Texture2D::TextureType::DEFAULT, Texture2D::TextureFormat::RGBA8_UNORM);

	m_Decoder = std::make_unique<ScreenSharingDecoder>(m_pd3dDevice.Get(), m_pd3dDeviceContext.Get());

	cuInit(0);

	// Get CUDA device
	CUdevice cuDevice = 0;
	cuDeviceGet(&cuDevice, 0);

	// Create CUDA context
	CUcontext cuContext = nullptr;
	cuCtxCreate(&cuContext, CU_CTX_SCHED_BLOCKING_SYNC, cuDevice);
	bool success = m_NvDecoderDX11.Initialize(cuContext, m_pd3dDevice, m_pd3dDeviceContext,
											  cudaVideoCodec_H264, // Example codec, adjust as needed
											  3840, 2160);

	m_NewOutputTexture.CreateWithCustomFlags(
		m_pd3dDevice.Get(),
		3840,
		2160,
		D3D11_BIND_SHADER_RESOURCE,
		D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED,
		Texture2D::TextureFormat::BGR8);

	m_DesktopCapture = std::make_unique<DesktopCapture>(m_pd3dDevice, m_pd3dDeviceContext);
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

bool App::initImGui()
{
	// Setup ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();

	// Enable features
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	// Setup style
	ImGui::StyleColorsDark();
	ImGuiStyle &style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer bindings
	if (!ImGui_ImplWin32_Init(m_Hwnd))
	{
		LOG_ERROR("Failed to initialize ImGui Win32 backend");
		return false;
	}

	if (!ImGui_ImplDX11_Init(m_pd3dDevice.Get(), m_pd3dDeviceContext.Get()))
	{
		LOG_ERROR("Failed to initialize ImGui DX11 backend");
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		return false;
	}

	// Enable DPI awareness
	ImGui_ImplWin32_EnableDpiAwareness();

	// Create samplers
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	HRESULT hr = m_pd3dDevice->CreateSamplerState(&samplerDesc, &m_pPointSampler);
	if (FAILED(hr))
	{
		LOG_ERROR("Failed to create sampler state: {0:x}", hr);
		return false;
	}

	LOG_INFO("ImGui initialized successfully");
	return true;
}

bool App::CreateDeviceD3D(HWND hWnd)
{
	MY_ASSERT(hWnd != nullptr, "Invalid window handle");

	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 2;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	// Create device
	// UINT createDeviceFlags = 0;
	UINT createDeviceFlags = 0;
#ifdef MYAPP_DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	// createDeviceFlags |= D3D11_CREATE_DEVICE_THR;

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
		// featureLevelArray,
		// 2,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		&sd,
		&m_pSwapChain,
		&m_pd3dDevice,
		&featureLevel,
		&m_pd3dDeviceContext);

	if (res == DXGI_ERROR_UNSUPPORTED)
	{
		LOG_WARN("Hardware device not supported, falling back to WARP device");
		// Try WARP device instead
		res = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			createDeviceFlags,
			// featureLevelArray,
			// 2,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			&sd,
			&m_pSwapChain,
			&m_pd3dDevice,
			&featureLevel,
			&m_pd3dDeviceContext);
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

	if (!CreateRenderTarget())
	{
		LOG_ERROR("Failed to create render target");
		return false;
	}

	return true;
}

bool App::CreateRenderTarget()
{
	MY_ASSERT(m_pSwapChain != nullptr, "Swap chain is null");
	MY_ASSERT(m_pd3dDevice != nullptr, "Device is null");

	// Create a render target view
	ID3D11Texture2D *pBackBuffer;
	HRESULT hr = m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr))
	{
		LOG_ERROR("Failed to get back buffer: {0:x}", hr);
		return false;
	}

	// hr = m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
	hr = m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, m_mainRenderTargetView.GetAddressOf());
	pBackBuffer->Release();

	if (FAILED(hr))
	{
		LOG_ERROR("Failed to create render target view: {0:x}", hr);
		return false;
	}

	LOG_INFO("Render target created successfully");
	return true;
}

void App::CleanupRenderTarget()
{
	if (m_mainRenderTargetView)
	{
		// m_mainRenderTargetView->Release();
		// m_mainRenderTargetView = nullptr;
		m_mainRenderTargetView.Reset();
	}
}

void App::CleanupDeviceD3D()
{
	LOG_INFO("Cleaning up D3D resources");

	CleanupRenderTarget();

	if (m_pSwapChain)
	{
		m_pSwapChain->Release();
		m_pSwapChain = nullptr;
	}

	if (m_pd3dDeviceContext)
	{
		m_pd3dDeviceContext->Release();
		m_pd3dDeviceContext = nullptr;
	}

	if (m_pd3dDevice)
	{
		m_pd3dDevice->Release();
		m_pd3dDevice = nullptr;
	}
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
	MY_ASSERT(m_pSwapChain != nullptr, "SwapChain is null in PerFrame");

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
		CleanupRenderTarget();
		HRESULT hr = m_pSwapChain->ResizeBuffers(0, m_ResizeWidth, m_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
		if (FAILED(hr))
		{
			LOG_ERROR("Failed to resize swap chain: {0:x}", hr);
		}
		m_ResizeWidth = m_ResizeHeight = 0;
		CreateRenderTarget();
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
	HRESULT hr = m_pSwapChain->Present(m_VSync, 0);
	if (hr == DXGI_STATUS_OCCLUDED)
	{
		m_SwapChainOccluded = true;
		LOG_INFO("Swap chain occluded");
	}
	else if (FAILED(hr))
	{
		LOG_ERROR("Present failed: {0:x}", hr);
		MY_ASSERT(false, "SwapChain Present failed");
	}
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

			Texture2D::CopyTexture(m_pd3dDeviceContext.Get(), m_DesktopCapture->GetTexture(), pTexBgra);

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
	MY_ASSERT(m_pd3dDeviceContext != nullptr, "Device context is null in onImGuiRender");
	MY_ASSERT(m_mainRenderTargetView != nullptr, "Render target view is null in onImGuiRender");

	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Create dockspace
	ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

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

	// Rendering
	ImGui::Render();

	// Clear the render target
	// m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, m_ClearColor);
	// m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
	m_pd3dDeviceContext->OMSetRenderTargets(1, m_mainRenderTargetView.GetAddressOf(), nullptr);

	// Render ImGui draw data
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	// Update and Render additional Platform Windows
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

// void App::onMessage()
// {
//     MSG msg;
//     while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
//     {
//         // Special handling for quit messages
//         if (msg.message == WM_QUIT)
//         {
//             LOG_INFO("Received WM_QUIT message");
//             m_Running = false;
//             return;
//         }

//         // Let ImGui process the message
//         if (!ImGui_ImplWin32_WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam))
//         {
//             ::TranslateMessage(&msg);
//             ::DispatchMessage(&msg);
//         }
//     }
// }

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
// void App::onMessage()
// {
// 	// Poll and handle messages (inputs, window resize, etc.)
// 	// See the WndProc() function below for our to dispatch events to the Win32 backend.
// 	MSG msg;
// 	while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
// 	{
// 		// log message
// 		// std::cout << "message: " << msg.message << std::endl;

// 		::TranslateMessage(&msg);
// 		// std::cout << "translated message" << std::endl;
// 		::DispatchMessage(&msg);
// 		// std::cout << "dispatched message" << std::endl;
// 		if (msg.message == WM_QUIT)
// 			m_Running = false;
// 	}
// }

void App::shutdown()
{
	LOG_INFO("Application shutting down");

	m_Running = false;

	if (m_pPointSampler)
	{
		m_pPointSampler->Release();
		m_pPointSampler = nullptr;
	}

	// Cleanup ImGui
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	// Cleanup D3D
	CleanupDeviceD3D();

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
	if (m_pd3dDevice != nullptr && width > 0 && height > 0)
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