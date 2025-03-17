#pragma once

#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>
#include <GameNetworkingSockets/steam/isteamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/steamnetworkingcustomsignaling.h>
#ifndef STEAMNETWORKINGSOCKETS_OPENSOURCE
#include <GameNetworkingSockets/steam/steam_api.h>
#endif

#include <string>
#include <map>
#include <thread>
#include <functional>
#include <iostream>
#include <chrono>
#include <vector>

#include <atomic>
#include <deque>
#include <mutex>

#include <winsock2.h>
#include <ws2tcpip.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
inline int GetSocketError() { return WSAGetLastError(); }
inline bool IgnoreSocketError(int e)
{
	return e == WSAEWOULDBLOCK || e == WSAENOTCONN;
}
#endif
class TrivialSignalingServer
{
public:
	enum class ConnectionStatus
	{
		Disconnected = 0,
		Connected,
		Connecting,
		FailedToConnect
	};

	TrivialSignalingServer() = default;
	~TrivialSignalingServer()
	{
		if (m_NetworkThread.joinable())
			m_NetworkThread.join();
	}

	void ConnectToServer(const std::string &serverAddress);

	void DisconnectFromServer()
	{
		m_Running.store(false);
	}

	void Send(const std::string &s);

	std::string GetConnectionDebugMessage() const { return m_ConnectionDebugMessage; }

	void ConnectToPeer(const SteamNetworkingIdentity &identityRemote);
	static HSteamNetConnection SendPeerConnectOffer(const SteamNetworkingIdentity &identityRemote);
	HSteamNetConnection GetConnection() const { return m_hConnection; }

private:
	// This is the thing we'll actually create to send signals for a particular
	// connection.
	struct ConnectionSignaling : ISteamNetworkingConnectionSignaling
	{
		TrivialSignalingServer *const m_pOwner;
		std::string const m_sPeerIdentity; // Save off the string encoding of the identity we're talking to

		ConnectionSignaling(TrivialSignalingServer *owner, const char *pszPeerIdentity)
			: m_pOwner(owner), m_sPeerIdentity(pszPeerIdentity)
		{
		}

		//
		// Implements ISteamNetworkingConnectionSignaling
		//

		// This is called from SteamNetworkingSockets to send a signal.  This could be called from any thread,
		// so we need to be threadsafe, and avoid duoing slow stuff or calling back into SteamNetworkingSockets
		bool SendSignal(HSteamNetConnection hConn, const SteamNetConnectionInfo_t &info, const void *pMsg, int cbMsg)
		{
			// Silence warnings
			(void)info;
			(void)hConn;

			// We'll use a dumb hex encoding.
			std::string signal;
			signal.reserve(m_sPeerIdentity.length() + cbMsg * 2 + 4);
			signal.append(m_sPeerIdentity);
			signal.push_back(' ');
			for (const uint8_t *p = (const uint8_t *)pMsg; cbMsg > 0; --cbMsg, ++p)
			{
				static const char hexdigit[] = "0123456789abcdef";
				signal.push_back(hexdigit[*p >> 4U]);
				signal.push_back(hexdigit[*p & 0xf]);
			}
			signal.push_back('\n');

			m_pOwner->Send(signal);
			return true;
		}

		// Self destruct.  This will be called by SteamNetworkingSockets when it's done with us.
		void Release()
		{
			delete this;
		}
	};
	void NetworkThreadFunc();

	void PollIncomingMessagesNew();

	static ISteamNetworkingConnectionSignaling *CreateSignalingForConnection(const SteamNetworkingIdentity &identityPeer);
	void SendMessageToPeer(const char *pszMsg);

private:
	std::thread m_NetworkThread;

	std::string m_ServerAddress;

	std::string m_ConnectionDebugMessage;

	std::atomic<bool> m_Running = false;
	std::atomic<ConnectionStatus> m_ConnectionStatus = ConnectionStatus::Disconnected;

	SOCKET m_Socket;

	ISteamNetworkingSockets *m_Interface;

	int m_nVirtualPortLocal = 0;  // Used when listening, and when connecting
	int m_nVirtualPortRemote = 0; // Only used when connecting

	HSteamNetConnection m_hConnection;

	std::deque<std::string> m_queueSend;
	std::recursive_mutex sockMutex;
};