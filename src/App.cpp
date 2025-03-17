#include "App.h"
#include <stdexcept>
#include <iostream>

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_win32.h"
#include "external/imgui/imgui_impl_dx11.h"

#include <windows.h>
#include <iostream>

#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>
#include "trivial_signaling_client.h"

HSteamListenSocket g_hListenSock;
HSteamNetConnection g_hConnection;
enum ETestRole
{
	k_ETestRole_Undefined,
	k_ETestRole_Server,
	k_ETestRole_Client,
	k_ETestRole_Symmetric,
};
ETestRole g_eTestRole = k_ETestRole_Undefined;

int g_nVirtualPortLocal = 0;  // Used when listening, and when connecting
int g_nVirtualPortRemote = 0; // Only used when connecting

std::unique_ptr<TrivialSignalingClient> pSignaling;

void Quit(int rc)
{
	if (rc == 0)
	{
		// OK, we cannot just exit the process, because we need to give
		// the connection time to actually send the last message and clean up.
		// If this were a TCP connection, we could just bail, because the OS
		// would handle it.  But this is an application protocol over UDP.
		// So give a little bit of time for good cleanup.  (Also note that
		// we really ought to continue pumping the signaling service, but
		// in this exampple we'll assume that no more signals need to be
		// exchanged, since we've gotten this far.)  If we just terminated
		// the program here, our peer could very likely timeout.  (Although
		// it's possible that the cleanup packets have already been placed
		// on the wire, and if they don't drop, things will get cleaned up
		// properly.)
		TEST_Printf("Waiting for any last cleanup packets.\n");
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

	TEST_Kill();
	exit(rc);
}

// Send a simple string message to out peer, using reliable transport.
void SendMessageToPeer(const char *pszMsg)
{
	TEST_Printf("Sending msg '%s'\n", pszMsg);
	EResult r = SteamNetworkingSockets()->SendMessageToConnection(
		g_hConnection, pszMsg, (int)strlen(pszMsg) + 1, k_nSteamNetworkingSend_Reliable, nullptr);
	assert(r == k_EResultOK);
}

void ConnecToPeer(const std::unique_ptr<TrivialSignalingClient> &pSignaling, const SteamNetworkingIdentity &identityRemote, SteamNetworkingErrMsg &errMsg)
{
	std::vector<SteamNetworkingConfigValue_t> vecOpts;

	// If we want the local and virtual port to differ, we must set
	// an option.  This is a pretty rare use case, and usually not needed.
	// The local virtual port is only usually relevant for symmetric
	// connections, and then, it almost always matches.  Here we are
	// just showing in this example code how you could handle this if you
	// needed them to differ.
	if (g_nVirtualPortRemote != g_nVirtualPortLocal)
	{
		SteamNetworkingConfigValue_t opt;
		opt.SetInt32(k_ESteamNetworkingConfig_LocalVirtualPort, g_nVirtualPortLocal);
		vecOpts.push_back(opt);
	}

	// Symmetric mode?  Noce that since we created a listen socket on this local
	// virtual port and tagged it for symmetric connect mode, any connections
	// we create that use the same local virtual port will automatically inherit
	// this setting.  However, this is really not recommended.  It is best to be
	// explicit.
	if (g_eTestRole == k_ETestRole_Symmetric)
	{
		SteamNetworkingConfigValue_t opt;
		opt.SetInt32(k_ESteamNetworkingConfig_SymmetricConnect, 1);
		vecOpts.push_back(opt);
		TEST_Printf("Connecting to '%s' in symmetric mode, virtual port %d, from local virtual port %d.\n",
					SteamNetworkingIdentityRender(identityRemote).c_str(), g_nVirtualPortRemote,
					g_nVirtualPortLocal);
	}
	else
	{
		TEST_Printf("Connecting to '%s', virtual port %d, from local virtual port %d.\n",
					SteamNetworkingIdentityRender(identityRemote).c_str(), g_nVirtualPortRemote,
					g_nVirtualPortLocal);
	}

	// Connect using the "custom signaling" path.  Note that when
	// you are using this path, the identity is actually optional,
	// since we don't need it.  (Your signaling object already
	// knows how to talk to the peer) and then the peer identity
	// will be confirmed via rendezvous.
	ISteamNetworkingConnectionSignaling *pConnSignaling = pSignaling->CreateSignalingForConnection(
		identityRemote,
		errMsg);
	assert(pConnSignaling);
	g_hConnection = SteamNetworkingSockets()->ConnectP2PCustomSignaling(pConnSignaling, &identityRemote, g_nVirtualPortRemote, (int)vecOpts.size(), vecOpts.data());
	assert(g_hConnection != k_HSteamNetConnection_Invalid);

	// Go ahead and send a message now.  The message will be queued until route finding
	// completes.
	SendMessageToPeer("Greetings!");
}

// Called when a connection undergoes a state transition.
void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo)
{
	// What's the state of the connection?
	switch (pInfo->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:

		TEST_Printf("[%s] %s, reason %d: %s\n",
					pInfo->m_info.m_szConnectionDescription,
					(pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ? "closed by peer" : "problem detected locally"),
					pInfo->m_info.m_eEndReason,
					pInfo->m_info.m_szEndDebug);

		// Close our end
		SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, nullptr, false);

		if (g_hConnection == pInfo->m_hConn)
		{
			g_hConnection = k_HSteamNetConnection_Invalid;

			// In this example, we will bail the test whenever this happens.
			// Was this a normal termination?
			int rc = 0;
			if (rc == k_ESteamNetworkingConnectionState_ProblemDetectedLocally || pInfo->m_info.m_eEndReason != k_ESteamNetConnectionEnd_App_Generic)
				rc = 1; // failure
			Quit(rc);
		}
		else
		{
			// Why are we hearing about any another connection?
			assert(false);
		}

		break;

	case k_ESteamNetworkingConnectionState_None:
		// Notification that a connection was destroyed.  (By us, presumably.)
		// We don't need this, so ignore it.
		break;

	case k_ESteamNetworkingConnectionState_Connecting:

		// Is this a connection we initiated, or one that we are receiving?
		if (g_hListenSock != k_HSteamListenSocket_Invalid && pInfo->m_info.m_hListenSocket == g_hListenSock)
		{
			// Somebody's knocking
			// Note that we assume we will only ever receive a single connection
			assert(g_hConnection == k_HSteamNetConnection_Invalid); // not really a bug in this code, but a bug in the test

			TEST_Printf("[%s] Accepting\n", pInfo->m_info.m_szConnectionDescription);
			g_hConnection = pInfo->m_hConn;
			SteamNetworkingSockets()->AcceptConnection(pInfo->m_hConn);
		}
		else
		{
			// Note that we will get notification when our own connection that
			// we initiate enters this state.
			assert(g_hConnection == pInfo->m_hConn);
			TEST_Printf("[%s] Entered connecting state\n", pInfo->m_info.m_szConnectionDescription);
		}
		break;

	case k_ESteamNetworkingConnectionState_FindingRoute:
		// P2P connections will spend a brief time here where they swap addresses
		// and try to find a route.
		TEST_Printf("[%s] finding route\n", pInfo->m_info.m_szConnectionDescription);
		break;

	case k_ESteamNetworkingConnectionState_Connected:
		// We got fully connected
		assert(pInfo->m_hConn == g_hConnection); // We don't initiate or accept any other connections, so this should be out own connection
		TEST_Printf("[%s] connected\n", pInfo->m_info.m_szConnectionDescription);
		break;

	default:
		assert(false);
		break;
	}
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

App *App::s_Instance = nullptr;

App::App(int argc, const char **argv)
{
	{
		// Initialize DXGI factory
		IDXGIFactory *pFactory = nullptr;
		if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&pFactory)))
		{
			std::cerr << "Failed to create DXGI factory" << std::endl;
			throw std::runtime_error("Failed to create DXGI factory");
		}

		// Enumerate adapters (graphics cards)
		IDXGIAdapter *pAdapter = nullptr;
		for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC adapterDesc;
			pAdapter->GetDesc(&adapterDesc);

			std::wcout << L"Adapter " << i << L": " << adapterDesc.Description << std::endl;

			// Enumerate outputs (monitors) for this adapter
			IDXGIOutput *pOutput = nullptr;
			for (UINT j = 0; pAdapter->EnumOutputs(j, &pOutput) != DXGI_ERROR_NOT_FOUND; ++j)
			{
				DXGI_OUTPUT_DESC outputDesc;
				pOutput->GetDesc(&outputDesc);

				// Print monitor device name
				std::wcout << L"  Monitor " << j << L": " << outputDesc.DeviceName << std::endl;

				// Now, let's get the current resolution of the monitor
				// We can get the display mode using EnumDisplaySettingsW
				DEVMODEW devMode; // Use DEVMODEW (wide-character version)
				ZeroMemory(&devMode, sizeof(devMode));
				devMode.dmSize = sizeof(devMode);

				// Enum the display settings for the monitor
				if (EnumDisplaySettingsW(outputDesc.DeviceName, ENUM_CURRENT_SETTINGS, &devMode))
				{
					std::wcout << L"    Resolution: " << devMode.dmPelsWidth << L"x" << devMode.dmPelsHeight << std::endl;
				}

				pOutput->Release();
			}

			pAdapter->Release();
		}

		pFactory->Release();
	}
	assert(s_Instance == nullptr);
	s_Instance = this;

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
		L"ImGui Example",
		nullptr};

	::RegisterClassExW(&m_Wc);
	m_Hwnd = ::CreateWindowW(m_Wc.lpszClassName, L"Dear ImGui DirectX11 Example", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, m_Wc.hInstance, nullptr);

	// Initialize Direct3D
	if (!CreateDeviceD3D(m_Hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClassW(m_Wc.lpszClassName, m_Wc.hInstance);
		throw std::runtime_error("Failed to create D3D device.");
	}

	// Show the window
	::ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(m_Hwnd);

	initImGui();

	initWinsock();

	//? networking
	{
		SteamNetworkingIdentity identityLocal;
		identityLocal.Clear();
		// SteamNetworkingIdentity identityRemote;
		m_identityRemote.Clear();
		const char *pszTrivialSignalingService = "localhost:10000";
		// const char *pszTrivialSignalingService = "141.148.233.31:6969";

		g_eTestRole = k_ETestRole_Symmetric;

		// Parse the command line
		for (int idxArg = 1; idxArg < argc; ++idxArg)
		{
			const char *pszSwitch = argv[idxArg];

			auto GetArg = [&]() -> const char *
			{
				if (idxArg + 1 >= argc)
					TEST_Fatal("Expected argument after %s", pszSwitch);
				return argv[++idxArg];
			};
			auto ParseIdentity = [&](SteamNetworkingIdentity &x)
			{
				const char *pszArg = GetArg();
				if (!x.ParseString(pszArg))
					TEST_Fatal("'%s' is not a valid identity string", pszArg);
			};

			if (!strcmp(pszSwitch, "--identity-local"))
				ParseIdentity(identityLocal);
			else if (!strcmp(pszSwitch, "--identity-remote"))
				ParseIdentity(m_identityRemote);
			else if (!strcmp(pszSwitch, "--signaling-server"))
				pszTrivialSignalingService = GetArg();
			else if (!strcmp(pszSwitch, "--log"))
			{
				const char *pszArg = GetArg();
				TEST_InitLog(pszArg);
			}
			else
				TEST_Fatal("Unexpected command line argument '%s'", pszSwitch);
		}

		if (identityLocal.IsInvalid())
			TEST_Fatal("Must specify local identity using --identity-local");
		if (m_identityRemote.IsInvalid() && g_eTestRole != k_ETestRole_Server)
			TEST_Fatal("Must specify remote identity using --identity-remote");

		// Initialize library, with the desired local identity
		TEST_Init(&identityLocal);

		// Hardcode STUN servers
		SteamNetworkingUtils()->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_STUN_ServerList, "stun.l.google.com:19302");

		// Hardcode TURN servers
		// comma seperated setting lists
		// const char* turnList = "turn:123.45.45:3478";
		// const char* userList = "username";
		// const char* passList = "pass";

		// SteamNetworkingUtils()->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_ServerList, turnList);
		// SteamNetworkingUtils()->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_UserList, userList);
		// SteamNetworkingUtils()->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_PassList, passList);

		// Allow sharing of any kind of ICE address.
		// We don't have any method of relaying (TURN) in this example, so we are essentially
		// forced to disclose our public address if we want to pierce NAT.  But if we
		// had relay fallback, or if we only wanted to connect on the LAN, we could restrict
		// to only sharing private addresses.
		SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_All);
		// SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_ip, k_nSteamNetworkingCon);
		// SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Public );
		// SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Private );

#if 0
		// Create the signaling service
		SteamNetworkingErrMsg errMsg;
		pSignaling = TrivialSignalingClient::CreateTrivialSignalingClient(pszTrivialSignalingService, SteamNetworkingSockets(), errMsg);
		if (pSignaling == nullptr)
			TEST_Fatal("Failed to initializing signaling client.  %s", errMsg);
		// m_pSignaling = pSignaling;
#endif

		//? throw error if not conencted to the server
		// pSignaling->Poll();

		SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(OnSteamNetConnectionStatusChanged);

		// Comment this line in for more detailed spew about signals, route finding, ICE, etc
		SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_LogLevel_P2PRendezvous, k_ESteamNetworkingSocketsDebugOutputType_Verbose);

		// Create listen socket to receive connections on, unless we are the client
		if (g_eTestRole == k_ETestRole_Symmetric)
		{

			// Currently you must create a listen socket to use symmetric mode,
			// even if you know that you will always create connections "both ways".
			// In the future we might try to remove this requirement.  It is a bit
			// less efficient, since it always triggered the race condition case
			// where both sides create their own connections, and then one side
			// decides to their theirs away.  If we have a listen socket, then
			// it can be the case that one peer will receive the incoming connection
			// from the other peer, and since he has a listen socket, can save
			// the connection, and then implicitly accept it when he initiates his
			// own connection.  Without the listen socket, if an incoming connection
			// request arrives before we have started connecting out, then we are forced
			// to ignore it, as the app has given no indication that it desires to
			// receive inbound connections at all.
			TEST_Printf("Creating listen socket in symmetric mode, local virtual port %d\n", g_nVirtualPortLocal);
			SteamNetworkingConfigValue_t opt;
			opt.SetInt32(k_ESteamNetworkingConfig_SymmetricConnect, 1); // << Note we set symmetric mode on the listen socket
			g_hListenSock = SteamNetworkingSockets()->CreateListenSocketP2P(g_nVirtualPortLocal, 1, &opt);
			assert(g_hListenSock != k_HSteamListenSocket_Invalid);
		}

#if 0
		// Begin connecting to peer, unless we are the server
		if (g_eTestRole != k_ETestRole_Server)
		{
			const char *localIdentityString = identityLocal.GetGenericString();
			TEST_Printf("Local identity: %s\n", localIdentityString);
			// if (argv[3])
			if (strcmp(localIdentityString, "peer_1") == 0)
			{

				ConnecToPeer(pSignaling, identityRemote, errMsg);
			}
		}
#endif
	}
	{
		m_NewTrivial.ConnectToServer("127.0.0.1:10000");
		// m_NewTrivial.GetConnectionDebugMessage();
		TEST_Printf("Connection debug message: %s\n", m_NewTrivial.GetConnectionDebugMessage().c_str());
	}
	// g_hConnection = m_NewTrivial.GetConnection();
}

bool App::CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	// sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	// createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = {
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_0,
	};
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}

void App::CreateRenderTarget()
{
	ID3D11Texture2D *pBackBuffer;
	m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
	pBackBuffer->Release();
}

void App::CleanupDeviceD3D()
{
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

void App::CleanupRenderTarget()
{
	if (m_mainRenderTargetView)
	{
		m_mainRenderTargetView->Release();
		m_mainRenderTargetView = nullptr;
	}
}

void App::initImGui()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;	  // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;	  // Enable Multi-Viewport / Platform Windows
	// io.ConfigViewportsNoAutoMerge = true;
	// io.ConfigViewportsNoTaskBarIcon = true;
	// io.ConfigViewportsNoDefaultParent = true;
	// io.ConfigDockingAlwaysTabBar = true;
	// io.ConfigDockingTransparentPayload = true;
	// io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: Experimental. THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP!
	// io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI: Experimental.

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	// ImGui::StyleColorsLight();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle &style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(m_Hwnd);
	ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	// io.Fonts->AddFontDefault();
	// io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
	// io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	// io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	// io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	// ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	// IM_ASSERT(font != nullptr);
}

void App::run()
{
	MSG msg;
	while (m_Running)
	{
		onMessage();
		if (m_Running == false)
		{
			break;
		}

		// Handle window being minimized or screen locked
		if (m_SwapChainOccluded && m_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
		{
			m_SwapChainOccluded = true;
			std::cout << "Window is occluded not rendering ui" << std::endl;
		}
		else
		{
			m_SwapChainOccluded = false;
		}

		// Handle window resize (we don't resize directly in the WM_SIZE handler)
		if (m_ResizeWidth != 0 && m_ResizeHeight != 0)
		{
			// m_DesktopCapture.stopCapture();
			CleanupRenderTarget();
			m_pSwapChain->ResizeBuffers(0, m_ResizeWidth, m_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			m_ResizeWidth = m_ResizeHeight = 0;
			CreateRenderTarget();
			// m_DesktopCapture.reset(m_pd3dDevice, m_pd3dDeviceContext, 0);
			// m_DesktopCapture.startCapture();
		}

		onUpdate();

		// Render the frame, but only if the window is not occluded.
		if (m_SwapChainOccluded == false)
		{
			onImGuiRender();
		}

		// Present
		HRESULT hr = m_pSwapChain->Present(1, 0); // Present with vsync
		// HRESULT hr = m_pSwapChain->Present(0, 0); // Present without vsync
		m_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
	}
}

void App::onUpdate()
{
	// return;
	// Check for incoming signals, and dispatch them
	// pSignaling->Poll();

	// Check callbacks
	TEST_PumpCallbacks();

	// If we have a connection, then poll it for messages
	if (g_hConnection != k_HSteamNetConnection_Invalid)
	{
		if (messageToSend.length() > 0)
		{
			std::cout << "Sending message to peer" << std::endl;
			SendMessageToPeer(messageToSend.c_str());
			messageToSend = "";
		}
		SteamNetworkingMessage_t *pMessage;
		int r = SteamNetworkingSockets()->ReceiveMessagesOnConnection(g_hConnection, &pMessage, 1);
		assert(r == 0 || r == 1); // <0 indicates an error
		if (r == 1)
		{
			// In this example code we will assume all messages are '\0'-terminated strings.
			// Obviously, this is not secure.
			TEST_Printf("Received message '%s'\n", pMessage->GetData());
			// std::string message = reinterpret_cast<char *>(pMessage->GetData());
			const char *message = reinterpret_cast<const char *>(pMessage->GetData());
			log(message);

			// Free message struct and buffer.
			pMessage->Release();

			// If we're the client, go ahead and shut down.  In this example we just
			// wanted to establish a connection and exchange a message, and we've done that.
			// Note that we use "linger" functionality.  This flushes out any remaining
			// messages that we have queued.  Essentially to us, the connection is closed,
			// but on thew wire, we will not actually close it until all reliable messages
			// have been confirmed as received by the client.  (Or the connection is closed
			// by the peer or drops.)  If we are the "client" role, then we know that no such
			// messages are in the pipeline in this test.  But in symmetric mode, it is
			// possible that we need to flush out our message that we sent.
			// if (g_eTestRole != k_ETestRole_Server)
			// {
			// 	TEST_Printf("Closing connection and shutting down.\n");
			// 	SteamNetworkingSockets()->CloseConnection(g_hConnection, 0, "Test completed OK", true);
			// 	break;
			// }

			// We're the server.  Send a reply.
			// SendMessageToPeer("I got your message");
			// if (messageToSend.length() > 0)
			// {
			// 	std::cout << "Sending message to peer" << std::endl;
			// 	SendMessageToPeer(messageToSend.c_str());
			// 	messageToSend = "";
			// }
			// log("I got your message");
			// std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	}
}

void App::onImGuiRender()
{

	ImGuiIO &io = ImGui::GetIO();
	(void)io;
	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

	ImGui::Begin("window 1");
	ImGui::Text("Hello, world!");
	ImGui::End();

	ImGui::Begin("Logs");
	for (const auto &log : m_Logs)
	{
		ImGui::Text(log.c_str());
	}
	ImGui::End();

	// text input
	{
		ImGui::Begin("input");
		static char buf[256] = "";
		ImGui::InputText("Input", buf, IM_ARRAYSIZE(buf));
		if (ImGui::Button("Send"))
		{
			// log(buf);
			// SendMessageToPeer(buf);
			messageToSend = buf;
			std::memset(buf, 0, sizeof(buf));
			std::cout << "Message sent: " << messageToSend << std::endl;
		}
		ImGui::End();
	}
	//? Connect to peer
	{
		ImGui::Begin("Connect to peer");
		static char buf[256] = "";
		ImGui::InputText("Peer username", buf, IM_ARRAYSIZE(buf));
		if (ImGui::Button("Send"))
		{
			m_NewTrivial.ConnectToPeer(m_identityRemote);
			g_hConnection = m_NewTrivial.GetConnection();
			// log(buf);
			// SendMessageToPeer(buf);
			// messageToSend = buf;
			// std::memset(buf, 0, sizeof(buf));
			// std::cout << "Message sent: " << messageToSend << std::endl;
		}
		ImGui::End();
	}

	// Rendering
	ImGui::Render();
	// const float clear_color_with_alpha[4] = {clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w};
	m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
	// g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha); //? Clear the main render target view, but with docking enabled this is not visible.
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	// Update and Render additional Platform Windows
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

void App::onMessage()
{
	// Poll and handle messages (inputs, window resize, etc.)
	// See the WndProc() function below for our to dispatch events to the Win32 backend.
	MSG msg;
	while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
	{
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
		if (msg.message == WM_QUIT)
			m_Running = false;
	}
}
void App::shutdown()
{
	// Cleanup
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(m_Hwnd);
	::UnregisterClassW(m_Wc.lpszClassName, m_Wc.hInstance);
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	App &app = App::get();

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		// m_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
		// m_ResizeHeight = (UINT)HIWORD(lParam);
		app.resize((UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	case WM_DPICHANGED:
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)
		{
			// const int dpi = HIWORD(wParam);
			// printf("WM_DPICHANGED to %d (%.0f%%)\n", dpi, (float)dpi / 96.0f * 100.0f);
			const RECT *suggested_rect = (RECT *)lParam;
			::SetWindowPos(hWnd, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		break;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void App::initWinsock()
{
	//? init winsock
	if (WSAStartup(MAKEWORD(2, 2), &m_WsaData) != 0)
	{
		std::cerr << "WSAStartup failed\n";
		std::exit(-1);
	}
}
