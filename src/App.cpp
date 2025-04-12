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

// fack you boost why are you hijack my cuda include
#include <cuda.h>
// #include <C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8/include/cuda.h>

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
	// EnumerateAdapters();

	m_Window = std::make_unique<Window>(L"My very cool app", 2560, 1440);
	m_Window->SetCustomWndProc(WndProc);
	if (!m_Window->Initialize())
	{
		LOG_ERROR("Failed to initialize window");
		throw std::runtime_error("Failed to initialize window");
	}

	m_Renderer = std::make_unique<Renderer>();
	m_Renderer->Initialize(m_Window->GetHandle(), 2560, 1440);
	m_Renderer->InitImGui(m_Window->GetHandle());
	// m_Renderer->

	//? init encoder
	// TODO: this is ugly, make my own wrapper for encoderoptions later
	// const char *argv[] = {"./build/Release/AppEncD3D11.exe", "-s", "3840x1440", "-codec",
	// 					  "h264", "-preset", "p1", "-tuninginfo", "ultralowlatency",
	// 					  "-bitrate", "25M", "-fps", "60", "-gop", "0"};
	const char *argv[] = {"-s", "3840x1440", "-codec",
						  "h264", "-preset", "p1", "-tuninginfo", "ultralowlatency",
						  "-bitrate", "25M", "-fps", "60", "-gop", "0"};

	// initEncoder(13, argv);
	NvEncoderInitParam encodeCLIOptions;
	// encodeCLIOptions.
	int nWidth = 0, nHeight = 0, temp = 0;
	ParseCommandLine_AppEncD3D(14, argv, nWidth, nHeight, encodeCLIOptions, temp, true);

	m_Enc = std::make_unique<NvEncoderD3D11>(m_Renderer->GetDevice(), 3840, 2160, NV_ENC_BUFFER_FORMAT_ARGB);

	InitializeEncoder(*m_Enc, encodeCLIOptions, NV_ENC_BUFFER_FORMAT_ARGB);

	m_DesktopCapture = std::make_unique<DesktopCapture>(m_Renderer->GetDevice(), m_Renderer->GetContext());
	m_DesktopCapture->InitDesktopDuplication();

	m_DecoderManager.SetDxDevice(m_Renderer->GetDevice(), m_Renderer->GetContext());
	m_DecoderManager.AddStream("Desktop Duplication", 3840, 2160);
	m_DecoderManager.AddStream("Desktop Duplication 2", 3840, 2160);

	LOG_INFO("App constructor completed successfully");
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
			// onMessage();
			m_Window->ProcessMessages();
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
	// Handle window resize
	if (m_ResizeWidth > 0 && m_ResizeHeight > 0)
	{
		LOG_INFO("Resizing swap chain: {0}x{1}", m_ResizeWidth, m_ResizeHeight);
		m_Renderer->Resize(m_ResizeWidth, m_ResizeHeight);
		m_Window->Resize(m_ResizeWidth, m_ResizeHeight);
		m_ResizeWidth = m_ResizeHeight = 0;
	}

	// Update application state
	{
		onUpdate();
	}

	// Render the frame if not occluded
	{
		onImGuiRender();
	}

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
				// successDecode = m_NvDecoderDX11.DecodeFrame(m_DecodedBuffer, m_NewOutputTexture.GetTexture());
				successDecode = m_DecoderManager.DecodeStream("Desktop Duplication", m_DecodedBuffer);
				successDecode = m_DecoderManager.DecodeStream("Desktop Duplication 2", m_DecodedBuffer);
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
				ImGui::Text("Window size: %dx%d", m_Window->GetWidth(), m_Window->GetHeight());

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
			// ImGui::Image((ImTextureID)m_DesktopCapture->GetTexture().GetShaderResourceView(), {3840, 2160});

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
			ImGui::Image(reinterpret_cast<ImTextureID>(m_DecoderManager.GetDecodedTextureSRV("Desktop Duplication")), imageSize);

			ImGui::End();
		}

		{
			ImGui::Begin("decoded texture 2");
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
			ImGui::Image(reinterpret_cast<ImTextureID>(m_DecoderManager.GetDecodedTextureSRV("Desktop Duplication 2")), imageSize);

			ImGui::End();
		}
	}

	m_Renderer->EndImGuiFrame();
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
	// if (m_Window->GetHandle())
	// {
	// 	::DestroyWindow(m_Hwnd);
	// 	m_Hwnd = nullptr;
	// }

	// // Unregister window class
	// if (m_Wc.hInstance)
	// {
	// 	::UnregisterClassW(m_Wc.lpszClassName, m_Wc.hInstance);
	// 	m_Wc = {};
	// }

	LOG_INFO("Application shutdown complete");
	s_Instance = nullptr;
}

void App::resize(UINT width, UINT height)
{
	if (width > 0 && height > 0)
	{
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
			// app.shutdown();
			app.Stop();
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
