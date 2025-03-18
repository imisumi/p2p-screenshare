#pragma once

#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>

#include <unordered_map>
#include "test_common.h"
#include "TrivialSignalingServer.h"
#include <string>

// namespace std
// {
// 	template <>
// 	struct hash<SteamNetworkingIdentity>
// 	{
// 		std::size_t operator()(const SteamNetworkingIdentity &identity) const
// 		{
// 			const char *str = identity.GetGenericString();
// 			if (str)
// 			{
// 				std::cout << "Hashing SteamNetworkingIdentity: " << str << std::endl;
// 				// It's a generic string, use string hash
// 				return std::hash<std::string>{}(str);
// 			}

// 			//? throw if str == nullptr
// 			// throw std::runtime_error("Failed to hash SteamNetworkingIdentity: GetGenericString() returned nullptr");

// 			std::cout << "Hashing SteamNetworkingIdentity: " << identity.m_steamID64 << std::endl;

// 			// Fallback for non-string identities
// 			return std::hash<int>{}(static_cast<int>(identity.m_eType)) ^
// 				   std::hash<uint64>{}(identity.m_steamID64);
// 		}
// 	};

// }

// namespace std
// {
// 	template <>
// 	struct hash<SteamNetworkingIdentity>
// 	{
// 		std::size_t operator()(const SteamNetworkingIdentity &identity) const
// 		{
// 			const char *str = identity.GetGenericString();
// 			if (!str)
// 			{
// 				throw std::runtime_error("Cannot hash SteamNetworkingIdentity: GetGenericString() returned nullptr");
// 			}
// 			return std::hash<std::string>{}(str);
// 		}
// 	};
// }

namespace std
{
	template <>
	struct hash<SteamNetworkingIdentity>
	{
		std::size_t operator()(const SteamNetworkingIdentity &identity) const
		{
			const char *str = identity.GetGenericString();
			if (str && *str)
			{ // Ensure it's not null and not an empty string
				return std::hash<std::string>{}(str);
			}
			// Fallback: hash the identity type instead
			return std::hash<int>{}(static_cast<int>(identity.m_eType));
		}
	};
}

class PeerConnections
{
public:
	enum class ConnectionStatus
	{
		Disconnected = 0,
		Connected,
		Connecting,
		FailedToConnect
	};
	HSteamNetConnection ConnectToPeer(const SteamNetworkingIdentity &identityRemote)
	{
		HSteamNetConnection connection = TrivialSignalingServer::SendPeerConnectOffer(identityRemote);
		if (connection == k_HSteamNetConnection_Invalid)
		{
			TEST_Printf("Failed to send connect request to '%s'\n", SteamNetworkingIdentityRender(identityRemote).c_str());
			// TODO: proper error handling
			throw std::runtime_error("Failed to send connect request");
		}
		// std::cout << "Generic sting: " << identityRemote.GetGenericString() << std::endl;
		std::string s = identityRemote.GetGenericString();
		std::cout << "Generic sting: " << s << std::endl;
		m_PeerConnections[identityRemote] = connection;
		SendToAllPeers("New peer connected");
		return connection;
	}

	// TODO: propper integration with accept connection
	void RegisterNewPeerConnection(const SteamNetworkingIdentity &identityPeer, HSteamNetConnection connection)
	{
		m_PeerConnections[identityPeer] = connection;
	}

	void RemovePeerConnection(const SteamNetworkingIdentity &identityPeer)
	{
		m_PeerConnections.erase(identityPeer);
	}

	HSteamNetConnection GetPeerConnection(const SteamNetworkingIdentity &identityPeer)
	{
		auto it = m_PeerConnections.find(identityPeer);
		if (it == m_PeerConnections.end())
		{
			TEST_Printf("Failed to get peer connection: connection not found\n");
			return k_HSteamNetConnection_Invalid;
		}
		return it->second;
	}

	void SendToPeer(const SteamNetworkingIdentity &identityPeer, const std::string &msg)
	{
		auto it = m_PeerConnections.find(identityPeer);
		if (it == m_PeerConnections.end())
		{
			TEST_Printf("Failed to send message to peer: connection not found\n");
			return;
		}

		//? length() + 1???
		EResult r = SteamNetworkingSockets()->SendMessageToConnection(
			it->second, msg.c_str(), msg.length() + 1, k_nSteamNetworkingSend_Reliable, nullptr);
		if (r != k_EResultOK)
		{
			TEST_Printf("Failed to send message to peer: %d\n", r);
		}
	}

	void SendToAllPeers(const std::string &msg)
	{
		for (auto &[peerIdentity, connection] : m_PeerConnections)
		{
			SendToPeer(peerIdentity, msg);
		}
	}

	void SetOutgoingMessage(const std::string &msg)
	{
		m_OutgoingMessage = msg;
	}

	std::string PollMessages()
	{
		//? First process incoming messages
		for (auto &[peerIdentity, connection] : m_PeerConnections)
		{
			if (connection == k_HSteamNetConnection_Invalid)
			{
				continue;
			}

			if (m_OutgoingMessage.empty())
			{
				continue;
			}

			// std::cout << "Sending message to peer" << std::endl;
			SendToPeer(peerIdentity, m_OutgoingMessage);
			m_OutgoingMessage.clear();
		}

		// TODO: temporary
		std::string incomingMessages;
		//? Process outgoing messages
		for (auto &[peerIdentity, connection] : m_PeerConnections)
		{
			if (connection == k_HSteamNetConnection_Invalid)
			{
				continue;
			}

			SteamNetworkingMessage_t *pMessage;
			int r = SteamNetworkingSockets()->ReceiveMessagesOnConnection(connection, &pMessage, 1);
			assert(r == 0 || r == 1); // <0 indicates an error
			if (r == 1)
			{
				std::string m = pMessage->m_identityPeer.GetGenericString();
				// In this example code we will assume all messages are '\0'-terminated strings.
				// Obviously, this is not secure.
				TEST_Printf("Received message '%s'\n", pMessage->GetData());
				// std::string message = reinterpret_cast<char *>(pMessage->GetData());
				const char *message = reinterpret_cast<const char *>(pMessage->GetData());
				m = m + ": " + message;
				// log(m);
				incomingMessages = m;
				// log(message);

				// Free message struct and buffer.
				pMessage->Release();
			}
		}
		return incomingMessages;
	}

	const std::unordered_map<SteamNetworkingIdentity, HSteamNetConnection>& GetPeerConnections() const
	{
		return m_PeerConnections;
	}

private:
	std::string m_OutgoingMessage;
	// std::unordered_map<HSteamNetConnection, SteamNetworkingIdentity> m_PeerConnections;
	std::unordered_map<SteamNetworkingIdentity, HSteamNetConnection> m_PeerConnections;
	// std::unordered_map<SteamNetworkingIdentity, HSteamNetConnection, SteamNetworkingIdentityHash> m_PeerConnections;
};