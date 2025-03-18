#include "TrivialSignalingServer.h"

#include <cassert>
#include "test_common.h"

static TrivialSignalingServer *s_Instance = nullptr;

inline int HexDigitVal(char c)
{
	if ('0' <= c && c <= '9')
		return c - '0';
	if ('a' <= c && c <= 'f')
		return c - 'a' + 0xa;
	if ('A' <= c && c <= 'F')
		return c - 'A' + 0xa;
	return -1;
}

void TrivialSignalingServer::ConnectToServer(const std::string &serverAddress)
{
	// assert if s_Instance is not null
	assert(s_Instance == nullptr);

	if (m_NetworkThread.joinable())
		m_NetworkThread.join();

	m_ServerAddress = serverAddress;
	m_NetworkThread = std::thread([this]()
								  { NetworkThreadFunc(); });
}

void TrivialSignalingServer::NetworkThreadFunc()
{
	s_Instance = this;

	std::string ip = m_ServerAddress.substr(0, m_ServerAddress.find(":"));
	std::string port = m_ServerAddress.substr(m_ServerAddress.find(":") + 1);

	m_Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_Socket == INVALID_SOCKET)
	{
		std::cerr << "Socket creation failed!\n";
		m_ConnectionStatus.store(ConnectionStatus::FailedToConnect);
		return;
	}

	// Set non-blocking mode
	u_long mode = 1; // 1 for non-blocking, 0 for blocking
	int result = ioctlsocket(m_Socket, FIONBIO, &mode);

	if (result == SOCKET_ERROR)
	{
		// Handle error
		int error = WSAGetLastError();
		std::cerr << "Failed to set non-blocking mode: " << error << "\n";
		m_ConnectionStatus.store(ConnectionStatus::FailedToConnect);
		closesocket(m_Socket);
		return;
		// Error handling code
	}

	// Set up server address struct
	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(std::stoi(port));

	if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0)
	{
		std::cerr << "Invalid address: " << ip << "\n";
		m_ConnectionStatus.store(ConnectionStatus::FailedToConnect);
		closesocket(m_Socket);
		return;
	}

	// Connect to server
	if (connect(m_Socket, (sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK)
		{
			std::cerr << "Connection failed with error: " << error << "\n";
			m_ConnectionStatus.store(ConnectionStatus::FailedToConnect);
			closesocket(m_Socket);
			return;
		}

		// For non-blocking, WSAEWOULDBLOCK is expected and means "in progress"
		m_ConnectionStatus.store(ConnectionStatus::Connecting);

		// You will need to check the connection status later using select()
		// This is typically done in a poll/event loop
	}
	else
	{
		// This is unlikely with non-blocking sockets, but just in case
		m_ConnectionStatus.store(ConnectionStatus::Connected);
	}

	std::cout << "Connected to server! 1\n";
	m_Interface = SteamNetworkingSockets();

	std::cout << "Connected to server! 2\n";

	SteamNetworkingIdentity identitySelf;
	identitySelf.Clear();
	m_Interface->GetIdentity(&identitySelf);
	std::cout << "Identity: " << identitySelf.GetGenericString() << "\n";
	assert(!identitySelf.IsInvalid());
	std::cout << "IsLocalHost: " << identitySelf.IsLocalHost() << "\n";
	assert(!identitySelf.IsLocalHost());

	//? we need this to register with the signaling sever
	// std::string identity = identitySelf.GetGenericString();
	std::string identity = SteamNetworkingIdentityRender(identitySelf).c_str();
	// identity = "str:" + identity;
	assert(strchr(identity.c_str(), ' ') == nullptr);
	identity.push_back('\n');

	std::cout << "Identity: " << identity << "\n";

	Send(identity);

	std::cout << "Connected to server!\n";
	m_ConnectionStatus.store(ConnectionStatus::Connected);

	// Start polling for messages
	m_Running.store(true);
	while (m_Running.load())
	{
		// std::cout << "Polling for messages...\n";
		PollIncomingMessagesNew();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	std::cout << "Disconnected from server.\n";
	closesocket(m_Socket);
	m_Socket = INVALID_SOCKET;
	m_ConnectionStatus.store(ConnectionStatus::Disconnected);
}

void TrivialSignalingServer::PollIncomingMessagesNew()
{
	sockMutex.lock();
	std::string m_sBufferedData;
	char buffer[4096];
	int bytesReceived = recv(m_Socket, buffer, sizeof(buffer) - 1, 0);

	if (bytesReceived > 0)
	{
		buffer[bytesReceived] = '\0'; // Null-terminate the received data
		std::cout << "Received: " << buffer << "\n";
		// TODO: handle this internally
		m_sBufferedData.append(buffer, bytesReceived);
	}
	else if (bytesReceived == 0)
	{
		std::cout << "Server closed connection.\n";
		m_Running.store(false);
	}
	else
	{
		// Error or would block
		int error = WSAGetLastError();
		if (error == WSAEWOULDBLOCK)
		{
			// No data available right now, this is normal for non-blocking
			// Just continue with other operations or try again later
		}
		else
		{
			// Actual error occurred
			std::cerr << "recv failed with error: " << error << "\n";
			// Handle error
		}
	}

	// Flush and send queued messages
	{
		while (!m_queueSend.empty())
		{
			const std::string &s = m_queueSend.front();
			// TEST_Printf("Sending signal: '%s'\n", s.c_str());
			int l = (int)s.length();
			int r = ::send(m_Socket, s.c_str(), l, 0);
			if (r < 0 && IgnoreSocketError(GetSocketError()))
				break;

			if (r == l)
			{
				m_queueSend.pop_front();
			}
			else if (r != 0)
			{
				// Socket hosed, or we sent a partial signal.
				// We need to restart connection
				TEST_Printf("Failed to send %d bytes to trivial signaling server.  send() returned %d, errno=%d.  Closing and restarting connection.\n",
							l, r, GetSocketError());
				// CloseSocket();
				break;
			}
		}
	}
	sockMutex.unlock();

	for (;;)
	{
		// break ;
		// Find end of line.  Do we have a complete signal?

		size_t l = m_sBufferedData.find('\n');
		if (l == std::string::npos)
			break;
		TEST_Printf("Received signal: '%s'\n", m_sBufferedData.substr(0, 50).c_str());

		// space count
		size_t first_space = m_sBufferedData.find(' ');
		if (first_space == std::string::npos)
			break;

		std::string type = m_sBufferedData.substr(0, first_space);

		m_sBufferedData.erase(0, first_space + 1);
		TEST_Printf("Remaining: '%s'\n", m_sBufferedData.substr(0, 50).c_str());

		l = m_sBufferedData.find('\n');
		if (l == std::string::npos)
			break;

		// Locate the space that seperates [from] [payload]
		size_t spc = m_sBufferedData.find(' ');
		if (spc != std::string::npos && spc < l)
		{

			// Hex decode the payload.  As it turns out, we actually don't
			// need the sender's identity.  The payload has everything needed
			// to process the message.  Maybe we should remove it from our
			// dummy signaling protocol?  It might be useful for debugging, tho.
			std::string data;
			data.reserve((l - spc) / 2);
			for (size_t i = spc + 1; i + 2 <= l; i += 2)
			{
				int dh = HexDigitVal(m_sBufferedData[i]);
				int dl = HexDigitVal(m_sBufferedData[i + 1]);
				if ((dh | dl) & ~0xf)
				{
					// Failed hex decode.  Not a bug in our code here, but this is just example code, so we'll handle it this way
					assert(!"Failed hex decode from signaling server?!");
					// goto next_message;
				}
				data.push_back((char)(dh << 4 | dl));
			}

			// Setup a context object that can respond if this signal is a connection request.
			struct Context : ISteamNetworkingSignalingRecvContext
			{
				TrivialSignalingServer *m_pOwner;

				virtual ISteamNetworkingConnectionSignaling *OnConnectRequest(
					HSteamNetConnection hConn,
					const SteamNetworkingIdentity &identityPeer,
					int nLocalVirtualPort) override
				{
					// Silence warnings
					(void)hConn;
					;
					(void)nLocalVirtualPort;

					// We will just always handle requests through the usual listen socket state
					// machine.  See the documentation for this function for other behaviour we
					// might take.

					// Also, note that if there was routing/session info, it should have been in
					// our envelope that we know how to parse, and we should save it off in this
					// context object.
					SteamNetworkingErrMsg ignoreErrMsg;
					return m_pOwner->CreateSignalingForConnection(identityPeer);
					// return m_pOwner->CreateSignalingForConnection(identityPeer, ignoreErrMsg);
				}

				virtual void SendRejectionSignal(
					const SteamNetworkingIdentity &identityPeer,
					const void *pMsg, int cbMsg) override
				{

					// We'll just silently ignore all failures.  This is actually the more secure
					// Way to handle it in many cases.  Actively returning failure might allow
					// an attacker to just scrape random peers to see who is online.  If you know
					// the peer has a good reason for trying to connect, sending an active failure
					// can improve error handling and the UX, instead of relying on timeout.  But
					// just consider the security implications.

					// Silence warnings
					(void)identityPeer;
					(void)pMsg;
					(void)cbMsg;
				}
			};
			Context context;
			context.m_pOwner = this;

			// Dispatch.
			// Remember: From inside this function, our context object might get callbacks.
			// And we might get asked to send signals, either now, or really at any time
			// from any thread!  If possible, avoid calling this function while holding locks.
			// To process this call, SteamnetworkingSockets will need take its own internal lock.
			// That lock may be held by another thread that is asking you to send a signal!  So
			// be warned that deadlocks are a possibility here.
			m_Interface->ReceivedP2PCustomSignal(data.c_str(), (int)data.length(), &context);
		}
	}
}

HSteamNetConnection TrivialSignalingServer::SendPeerConnectOffer(const SteamNetworkingIdentity &identityRemote)
{
	std::vector<SteamNetworkingConfigValue_t> vecOpts;

	int m_nVirtualPortRemote = 0;
	int m_nVirtualPortLocal = 0;

	// If we want the local and virtual port to differ, we must set
	// an option.  This is a pretty rare use case, and usually not needed.
	// The local virtual port is only usually relevant for symmetric
	// connections, and then, it almost always matches.  Here we are
	// just showing in this example code how you could handle this if you
	// needed them to differ.
	if (m_nVirtualPortRemote != m_nVirtualPortLocal)
	{
		SteamNetworkingConfigValue_t opt;
		opt.SetInt32(k_ESteamNetworkingConfig_LocalVirtualPort, m_nVirtualPortLocal);
		vecOpts.push_back(opt);
	}

	// Symmetric mode?  Noce that since we created a listen socket on this local
	// virtual port and tagged it for symmetric connect mode, any connections
	// we create that use the same local virtual port will automatically inherit
	// this setting.  However, this is really not recommended.  It is best to be
	// explicit.
	{
		SteamNetworkingConfigValue_t opt;
		opt.SetInt32(k_ESteamNetworkingConfig_SymmetricConnect, 1);
		vecOpts.push_back(opt);
		TEST_Printf("Connecting to '%s' in symmetric mode, virtual port %d, from local virtual port %d.\n",
					SteamNetworkingIdentityRender(identityRemote).c_str(), m_nVirtualPortRemote,
					m_nVirtualPortLocal);
	}
	// Connect using the "custom signaling" path.  Note that when
	// you are using this path, the identity is actually optional,
	// since we don't need it.  (Your signaling object already
	// knows how to talk to the peer) and then the peer identity
	// will be confirmed via rendezvous.
	// ISteamNetworkingConnectionSignaling *pConnSignaling = CreateSignalingForConnection(
	// 	identityRemote,
	// 	errMsg);
	ISteamNetworkingConnectionSignaling *pConnSignaling = CreateSignalingForConnection(
		identityRemote);
	assert(pConnSignaling);
	HSteamNetConnection m_hConnection = SteamNetworkingSockets()->ConnectP2PCustomSignaling(pConnSignaling, &identityRemote, m_nVirtualPortRemote, (int)vecOpts.size(), vecOpts.data());
	//TODO: 
	// assert(m_hConnection != k_HSteamNetConnection_Invalid);

	// Go ahead and send a message now.  The message will be queued until route finding
	// completes.
	// SendMessageToPeer("Greetings!");
	return m_hConnection;
}
// HSteamNetConnection TrivialSignalingServer::SendPeerConnectOffer(const SteamNetworkingIdentity &identityRemote)
// {
// 	// asert if s_Instance is null
// 	assert(s_Instance != nullptr);

// 	std::vector<SteamNetworkingConfigValue_t> vecOpts;
// 	int m_nVirtualPortRemote = 0;
// 		int m_nVirtualPortLocal = 0;

// 	// If we want the local and virtual port to differ, we must set
// 	// an option.  This is a pretty rare use case, and usually not needed.
// 	// The local virtual port is only usually relevant for symmetric
// 	// connections, and then, it almost always matches.  Here we are
// 	// just showing in this example code how you could handle this if you
// 	// needed them to differ.
// 	if (m_nVirtualPortRemote != m_nVirtualPortLocal)
// 	{
// 		SteamNetworkingConfigValue_t opt;
// 		opt.SetInt32(k_ESteamNetworkingConfig_LocalVirtualPort, m_nVirtualPortLocal);
// 		vecOpts.push_back(opt);
// 	}

// 	// Symmetric mode?  Noce that since we created a listen socket on this local
// 	// virtual port and tagged it for symmetric connect mode, any connections
// 	// we create that use the same local virtual port will automatically inherit
// 	// this setting.  However, this is really not recommended.  It is best to be
// 	// explicit.
// 	{
// 		SteamNetworkingConfigValue_t opt;
// 		opt.SetInt32(k_ESteamNetworkingConfig_SymmetricConnect, 1);
// 		vecOpts.push_back(opt);
// 		TEST_Printf("Connecting to '%s' in symmetric mode, virtual port %d, from local virtual port %d.\n",
// 					SteamNetworkingIdentityRender(identityRemote).c_str(), m_nVirtualPortRemote,
// 					m_nVirtualPortLocal);
// 	}
// 	// Connect using the "custom signaling" path.  Note that when
// 	// you are using this path, the identity is actually optional,
// 	// since we don't need it.  (Your signaling object already
// 	// knows how to talk to the peer) and then the peer identity
// 	// will be confirmed via rendezvous.
// 	// ISteamNetworkingConnectionSignaling *pConnSignaling = CreateSignalingForConnection(
// 	// 	identityRemote,
// 	// 	errMsg);
// 	ISteamNetworkingConnectionSignaling *pConnSignaling = CreateSignalingForConnection(
// 		identityRemote);
// 	assert(pConnSignaling);
// 	HSteamNetConnection connection = SteamNetworkingSockets()->ConnectP2PCustomSignaling(pConnSignaling, &identityRemote, m_nVirtualPortRemote, (int)vecOpts.size(), vecOpts.data());
// 	if (connection == k_HSteamNetConnection_Invalid)
// 	{
// 		TEST_Printf("Failed to send connect request to '%s'\n", SteamNetworkingIdentityRender(identityRemote).c_str());
// 	}
// 	return connection;
// }

void TrivialSignalingServer::ConnectToPeer(const SteamNetworkingIdentity &identityRemote)
{
	std::vector<SteamNetworkingConfigValue_t> vecOpts;

	// If we want the local and virtual port to differ, we must set
	// an option.  This is a pretty rare use case, and usually not needed.
	// The local virtual port is only usually relevant for symmetric
	// connections, and then, it almost always matches.  Here we are
	// just showing in this example code how you could handle this if you
	// needed them to differ.
	if (m_nVirtualPortRemote != m_nVirtualPortLocal)
	{
		SteamNetworkingConfigValue_t opt;
		opt.SetInt32(k_ESteamNetworkingConfig_LocalVirtualPort, m_nVirtualPortLocal);
		vecOpts.push_back(opt);
	}

	// Symmetric mode?  Noce that since we created a listen socket on this local
	// virtual port and tagged it for symmetric connect mode, any connections
	// we create that use the same local virtual port will automatically inherit
	// this setting.  However, this is really not recommended.  It is best to be
	// explicit.
	{
		SteamNetworkingConfigValue_t opt;
		opt.SetInt32(k_ESteamNetworkingConfig_SymmetricConnect, 1);
		vecOpts.push_back(opt);
		TEST_Printf("Connecting to '%s' in symmetric mode, virtual port %d, from local virtual port %d.\n",
					SteamNetworkingIdentityRender(identityRemote).c_str(), m_nVirtualPortRemote,
					m_nVirtualPortLocal);
	}
	// Connect using the "custom signaling" path.  Note that when
	// you are using this path, the identity is actually optional,
	// since we don't need it.  (Your signaling object already
	// knows how to talk to the peer) and then the peer identity
	// will be confirmed via rendezvous.
	// ISteamNetworkingConnectionSignaling *pConnSignaling = CreateSignalingForConnection(
	// 	identityRemote,
	// 	errMsg);
	ISteamNetworkingConnectionSignaling *pConnSignaling = CreateSignalingForConnection(
		identityRemote);
	assert(pConnSignaling);
	m_hConnection = SteamNetworkingSockets()->ConnectP2PCustomSignaling(pConnSignaling, &identityRemote, m_nVirtualPortRemote, (int)vecOpts.size(), vecOpts.data());
	assert(m_hConnection != k_HSteamNetConnection_Invalid);

	// Go ahead and send a message now.  The message will be queued until route finding
	// completes.
	SendMessageToPeer("Greetings!");
}

ISteamNetworkingConnectionSignaling *TrivialSignalingServer::CreateSignalingForConnection(const SteamNetworkingIdentity &identityPeer)
{
	assert(s_Instance != nullptr);
	SteamNetworkingIdentityRender sIdentityPeer(identityPeer);

	// FIXME - here we really ought to confirm that the string version of the
	// identity does not have spaces, since our protocol doesn't permit it.
	TEST_Printf("Creating signaling session for peer '%s'\n", sIdentityPeer.c_str());

	// Silence warnings
	// (void)errMsg;

	// return new ConnectionSignaling(this, sIdentityPeer.c_str());
	return new ConnectionSignaling(s_Instance, sIdentityPeer.c_str());
}

void TrivialSignalingServer::SendMessageToPeer(const char *pszMsg)
{
	TEST_Printf("Sending msg '%s'\n", pszMsg);
	EResult r = SteamNetworkingSockets()->SendMessageToConnection(
		m_hConnection, pszMsg, (int)strlen(pszMsg) + 1, k_nSteamNetworkingSend_Reliable, nullptr);
	assert(r == k_EResultOK);
}

void TrivialSignalingServer::Send(const std::string &s)
{
	assert(s.length() > 0 && s[s.length() - 1] == '\n');

	sockMutex.lock();

	while (m_queueSend.size() > 32)
	{
		TEST_Printf("Signaling send queue is backed up.  Discarding oldest signals\n");
		m_queueSend.pop_front();
	}

	m_queueSend.push_back(s);
	sockMutex.unlock();
}
