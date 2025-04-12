#pragma once

// // #include <d3d11.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <tchar.h>
#include <iostream>
#include <array>
#include <vector>
#include <algorithm>
#include <chrono>
#include <span>
#include <memory>

#include "Texture/Texture2D.h"

#include "Log.h"
#include "Decode.h"

#include "DesktopDuplication.h"

#include <d3d11.h>
#include <windows.h>
#include <string>
#include "Log.h"

#include "Codec/NvCodec/NvEncoder/NvEncoderD3D11.h"

#include "Renderer.h"
#include "Window.h"

#include "Codec/Encode/AppEncD3D11.h"

#include "DecodingManager.h"

class App
{
public:
	App();
	~App()
	{
		LOG_INFO("App destructor started");
	}

	void run();
	void shutdown();

	// Accessors
	static App &get();
	bool isRunning() const { return m_Running; }
	float getDeltaTime() const { return m_DeltaTime; }

	void Stop() { m_Running = false; }
	// Window management
	void resize(UINT width, UINT height);
	void PerFrame();

	void EnableVSync() { m_VSync = true; }
	void DisableVSync() { m_VSync = false; }

protected:
	// Virtual functions for derived classes to override
	virtual void onUpdate();
	virtual void onImGuiRender();

private:
	// Window state
	// Singleton instance
	static App *s_Instance;

	bool m_Running = false;
	UINT m_ResizeWidth = 0;
	UINT m_ResizeHeight = 0;
	UINT m_WindowWidth = 1280;
	UINT m_WindowHeight = 800;
	float m_DeltaTime = 0.0f;

	bool m_VSync = false; // VSync flag

	std::chrono::time_point<std::chrono::high_resolution_clock> m_lastEncodeTime;
	const double m_targetFrameTimeMs = 1000.0 / 60.0; // For 60 FPS (16.67ms)

	std::chrono::time_point<std::chrono::high_resolution_clock> m_LastCaptureTime;
	const double m_TargetFrameCaptureTimesMs = 1000.0 / 240.0; // For 240 FPS (4.17ms)

	bool successDecode = false;
	std::vector<uint8_t> m_DecodedBuffer;

	std::shared_ptr<DesktopCapture> m_DesktopCapture;

	std::unique_ptr<NvEncoderD3D11> m_Enc;
	DecodingManager m_DecoderManager;

	std::unique_ptr<Renderer> m_Renderer;
	std::unique_ptr<Window> m_Window;
};