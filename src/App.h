#pragma once

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

#include "Networking/TrivialSignalingServer.h"
#include "Networking/PeerConnections.h"

class App
{
public:
	enum class ConnectionStatus
	{
		Disconnected = 0,
		Connected,
		Connecting,
		FailedToConnect
	};
	App(int argc, const char **argv);
	~App()
	{
		m_NewTrivial.DisconnectFromServer();
		GameNetworkingSockets_Kill();
	}

	// void init();
	void run();
	void shutdown();

	inline static App &get() { return *s_Instance; }

	void resize(UINT width, UINT height)
	{
		m_ResizeWidth = width;
		m_ResizeHeight = height;
	}

	void log(const std::string &msg)
	{
		m_Logs.push_back(msg);
	}

	static App& Get();

	PeerConnections &GetPeerConnections() { return m_PeerConnections; }

private:
	void onUpdate();
	void initImGui();
	void onMessage();
	void onImGuiRender();

	bool CreateDeviceD3D(HWND hWnd);
	void CreateRenderTarget();
	void CleanupDeviceD3D();
	void CleanupRenderTarget();

	void initWinsock();

private:
	bool m_Running = true;
	HWND m_Hwnd = nullptr;
	WNDCLASSEXW m_Wc;
	ID3D11Device *m_pd3dDevice = nullptr;
	ID3D11DeviceContext *m_pd3dDeviceContext = nullptr;
	IDXGISwapChain *m_pSwapChain = nullptr;
	bool m_SwapChainOccluded = false;
	UINT m_ResizeWidth = 0, m_ResizeHeight = 0;
	ID3D11RenderTargetView *m_mainRenderTargetView = nullptr;

	static App *s_Instance;

	//? netowrking
	WSADATA m_WsaData;
	bool m_Lisening = false;

	std::vector<std::string> m_Logs;

	std::string messageToSend;

	TrivialSignalingServer m_NewTrivial;

	SteamNetworkingIdentity m_identityRemote;

	PeerConnections m_PeerConnections;
};
