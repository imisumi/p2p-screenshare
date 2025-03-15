#pragma once

#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>

#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include <span>

//! ty checno <3
class SignalServerClient
{
public:
	enum class ConnectionStatus
	{
		Disconnected = 0,
		Connected,
		Connecting,
		FailedToConnect
	};

	using DataReceivedCallback = std::function<void(std::span<const std::byte> data)>;
	using ServerConnectedCallback = std::function<void()>;
	using ServerDisconnectedCallback = std::function<void()>;

	SignalServerClient();
	~SignalServerClient();

	void ConnectToServer(const std::string &serverAddress);
	void Disconnect();

	ConnectionStatus GetConnectionStatus() const { return m_ConnectionStatus; }

private:
	void NetworkThreadFunc(); // Server thread
	void PollIncomingMessages();
	void PollConnectionStateChanges();
	static void ConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *info);
	void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *info);

	void SetDataReceivedCallback(const DataReceivedCallback &function);
	void SetServerConnectedCallback(const ServerConnectedCallback &function);
	void SetServerDisconnectedCallback(const ServerDisconnectedCallback &function);

private:
	std::thread m_NetworkThread;
	DataReceivedCallback m_DataReceivedCallback;
	ServerConnectedCallback m_ServerConnectedCallback;
	ServerDisconnectedCallback m_ServerDisconnectedCallback;

	std::atomic<bool> m_Running = false;
	std::string m_ServerAddress;
	ConnectionStatus m_ConnectionStatus = ConnectionStatus::Disconnected;

	ISteamNetworkingSockets *m_Interface = nullptr;
	HSteamNetConnection m_Connection = 0;
	std::string m_ConnectionDebugMessage;
};