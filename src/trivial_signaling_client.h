
#pragma once
#include "test_common.h"

#include <string>
#include <mutex>
#include <deque>
#include <assert.h>
#include <memory>

// #include "trivial_signaling_client.h"
#include <GameNetworkingSockets/steam/isteamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>
#include <GameNetworkingSockets/steam/steamnetworkingcustomsignaling.h>
// #include <steam/steamnetworkingcustomsignaling.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
typedef int SOCKET;
constexpr SOCKET INVALID_SOCKET = -1;
inline void closesocket(SOCKET s) { close(s); }
inline int GetSocketError() { return errno; }
inline bool IgnoreSocketError(int e)
{
	return e == EAGAIN || e == ENOTCONN || e == EWOULDBLOCK;
}
#ifndef ioctlsocket
#define ioctlsocket ioctl
#endif
#endif
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

/// Implementation of ITrivialSignalingClient
// class TrivialSignalingClient : public ITrivialSignalingClient
class TrivialSignalingClient
{

	// This is the thing we'll actually create to send signals for a particular
	// connection.
	struct ConnectionSignaling : ISteamNetworkingConnectionSignaling
	{
		TrivialSignalingClient *const m_pOwner;
		std::string const m_sPeerIdentity; // Save off the string encoding of the identity we're talking to

		ConnectionSignaling(TrivialSignalingClient *owner, const char *pszPeerIdentity)
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

	sockaddr_storage m_adrServer;
	size_t const m_adrServerSize;
	ISteamNetworkingSockets *const m_pSteamNetworkingSockets;
	std::string m_sGreeting;
	std::deque<std::string> m_queueSend;

	std::recursive_mutex sockMutex;
	SOCKET m_sock;
	std::string m_sBufferedData;

	void CloseSocket();

	void Connect();

public:
	TrivialSignalingClient(const sockaddr *adrServer, size_t adrServerSize, ISteamNetworkingSockets *pSteamNetworkingSockets, const char *pszServerAddress);

	// Send the signal.
	void Send(const std::string &s);

	ISteamNetworkingConnectionSignaling *CreateSignalingForConnection(
		const SteamNetworkingIdentity &identityPeer,
		SteamNetworkingErrMsg &errMsg);

	void Poll();

	void Release();

	static std::unique_ptr<TrivialSignalingClient> CreateTrivialSignalingClient(
		const char *pszServerAddress,					  // Address of the server.
		ISteamNetworkingSockets *pSteamNetworkingSockets, // Where should we send signals when we get them?
		SteamNetworkingErrMsg &errMsg					  // Error message is retjrned here if we fail
	);
};

// Start connecting to the signaling server.
// TrivialSignalingClient *CreateTrivialSignalingClient(
// 	const char *pszServerAddress,					  // Address of the server.
// 	ISteamNetworkingSockets *pSteamNetworkingSockets, // Where should we send signals when we get them?
// 	SteamNetworkingErrMsg &errMsg					  // Error message is retjrned here if we fail
// );