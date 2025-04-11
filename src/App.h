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

// #include "desktop-duplication/DesktopCapture.h"

#include "Texture/Texture2D.h"

// #include "Texture/Texture2D.h"

#include "Log.h"
#include "Decode.h"

#include "Codec/Decoder.h"


#include "DesktopDuplication.h"

#include <d3d11.h>
#include <windows.h>
#include <string>
#include "Log.h"

#include "Codec/NvCodec/NvEncoder/NvEncoderD3D11.h"
#include "Codec/Utils/NvCodecUtils.h"
#include "Codec/Utils/NvEncoderCLIOptions.h"

#include "Codec/Encode/Common/AppEncUtils.h"

#include "Renderer.h"

int initEncoder(int argc, const char **argv);
template <class EncoderClass>
void InitializeEncoder(EncoderClass &enc, NvEncoderInitParam encodeCLIOptions, NV_ENC_BUFFER_FORMAT eFormat);

class App
{
public:
	App();
	~App() = default;

	void run();
	void shutdown();

	// Accessors
	static App &get();
	// ID3D11Device *getDevice() const { return m_pd3dDevice.Get(); }
	// ID3D11DeviceContext *getDeviceContext() const { return m_pd3dDeviceContext.Get(); }
	HWND getWindow() const { return m_Hwnd; }
	bool isRunning() const { return m_Running; }
	float getDeltaTime() const { return m_DeltaTime; }

	// Window management
	void resize(UINT width, UINT height);
	void PerFrame();

	// Message processing
	void onMessage();

	void EnableVSync() { m_VSync = true; }
	void DisableVSync() { m_VSync = false; }

protected:
	// Virtual functions for derived classes to override
	virtual void onUpdate();
	virtual void onImGuiRender();

private:
	// Window initialization
	bool InitWindow();

	// Device enumeration
	void EnumerateAdapters();
	void EnumerateOutputs(IDXGIAdapter *pAdapter, UINT adapterIndex);
	std::string ws2s(const std::wstring &wstr);

	// Window resources
	WNDCLASSEXW m_Wc = {};
	HWND m_Hwnd = nullptr;

	// Window state
	bool m_Running = false;
	bool m_SwapChainOccluded = false;
	UINT m_ResizeWidth = 0;
	UINT m_ResizeHeight = 0;
	UINT m_WindowWidth = 1280;
	UINT m_WindowHeight = 800;
	float m_DeltaTime = 0.0f;

	bool m_VSync = false; // VSync flag

	// Singleton instance
	static App *s_Instance;

	std::unique_ptr<NvEncoderD3D11> m_Enc;
	Texture2D m_EncodedTexture;

	std::chrono::time_point<std::chrono::high_resolution_clock> m_lastEncodeTime;
	const double m_targetFrameTimeMs = 1000.0 / 60.0; // For 60 FPS (16.67ms)

	std::chrono::time_point<std::chrono::high_resolution_clock> m_LastCaptureTime;
	const double m_TargetFrameCaptureTimesMs = 1000.0 / 240.0; // For 240 FPS (4.17ms)


	std::shared_ptr<ScreenSharingDecoder > m_Decoder;
	NvDecoderDX11 m_NvDecoderDX11;


	ID3D11Texture2D* texture = nullptr;

	bool successDecode= false;
	std::vector<uint8_t> m_DecodedBuffer;

	Texture2D m_NewOutputTexture;



	std::shared_ptr<DesktopCapture> m_DesktopCapture;

	std::unique_ptr<Renderer> m_Renderer;
};