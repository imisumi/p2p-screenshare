#include "trivial_signaling_client.h"

#include <vector>
#include <sstream>

void TrivialSignalingClient::CloseSocket()
{
	if (m_sock != INVALID_SOCKET)
	{
		closesocket(m_sock);
		m_sock = INVALID_SOCKET;
	}
	m_sBufferedData.clear();
	m_queueSend.clear();
}

void TrivialSignalingClient::Connect()
{
	CloseSocket();

	int sockType = SOCK_STREAM;
#ifdef LINUX
	sockType |= SOCK_CLOEXEC;
#endif
	m_sock = socket(m_adrServer.ss_family, sockType, IPPROTO_TCP);
	if (m_sock == INVALID_SOCKET)
	{
		TEST_Printf("socket() failed, error=%d\n", GetSocketError());
		return;
	}

	// Request nonblocking IO
	unsigned long opt = 1;
	if (ioctlsocket(m_sock, FIONBIO, &opt) == -1)
	{
		CloseSocket();
		TEST_Printf("ioctlsocket() failed, error=%d\n", GetSocketError());
		return;
	}

	connect(m_sock, (const sockaddr *)&m_adrServer, (socklen_t)m_adrServerSize);

	// And immediate send our greeting.  This just puts in in the buffer and
	// it will go out once the socket connects.
	Send(m_sGreeting);
}

TrivialSignalingClient::TrivialSignalingClient(const sockaddr *adrServer, size_t adrServerSize, ISteamNetworkingSockets *pSteamNetworkingSockets, const char *pszServerAddress)
	: m_adrServerSize(adrServerSize), m_pSteamNetworkingSockets(pSteamNetworkingSockets)
{
	memcpy(&m_adrServer, adrServer, adrServerSize);
	m_sock = INVALID_SOCKET;

	// Save off our identity
	SteamNetworkingIdentity identitySelf;
	identitySelf.Clear();
	pSteamNetworkingSockets->GetIdentity(&identitySelf);
	assert(!identitySelf.IsInvalid());
	assert(!identitySelf.IsLocalHost()); // We need something more specific than that
	m_sGreeting = SteamNetworkingIdentityRender(identitySelf).c_str();
	assert(strchr(m_sGreeting.c_str(), ' ') == nullptr); // Our protocol is dumb and doesn't support this
	m_sGreeting.push_back('\n');

	// Begin connecting immediately
	Connect();
}

// Send the signal.
void TrivialSignalingClient::Send(const std::string &s)
{
	assert(s.length() > 0 && s[s.length() - 1] == '\n'); // All of our signals are '\n'-terminated

	sockMutex.lock();

	// If we're getting backed up, delete the oldest entries.  Remember,
	// we are only required to do best-effort delivery.  And old signals are the
	// most likely to be out of date (either old data, or the client has already
	// timed them out and queued a retry).
	while (m_queueSend.size() > 32)
	{
		TEST_Printf("Signaling send queue is backed up.  Discarding oldest signals\n");
		m_queueSend.pop_front();
	}

	m_queueSend.push_back(s);
	sockMutex.unlock();
}

ISteamNetworkingConnectionSignaling *TrivialSignalingClient::CreateSignalingForConnection(
	const SteamNetworkingIdentity &identityPeer,
	SteamNetworkingErrMsg &errMsg)
{
	SteamNetworkingIdentityRender sIdentityPeer(identityPeer);

	// FIXME - here we really ought to confirm that the string version of the
	// identity does not have spaces, since our protocol doesn't permit it.
	TEST_Printf("Creating signaling session for peer '%s'\n", sIdentityPeer.c_str());

	// Silence warnings
	(void)errMsg;

	return new ConnectionSignaling(this, sIdentityPeer.c_str());
}

// void PrintHumanReadable(const std::string& data)
// {
//     // Print size information
//     TEST_Printf("Received data: %zu bytes\n", data.size());

//     // Hex output - 16 bytes per line
//     TEST_Printf("Hex representation:\n");
//     char hexLine[16*3 + 1]; // Each byte becomes 2 hex chars + 1 space, plus null terminator
//     char asciiLine[17];     // 16 chars + null terminator

//     for (size_t i = 0; i < data.size(); i += 16) {
//         // Format position
//         TEST_Printf("%04zx: ", i);

//         // Clear buffers
//         memset(hexLine, ' ', sizeof(hexLine) - 1);
//         hexLine[sizeof(hexLine) - 1] = 0;
//         memset(asciiLine, 0, sizeof(asciiLine));

//         // Process up to 16 bytes
//         for (size_t j = 0; j < 16 && i + j < data.size(); j++) {
//             unsigned char c = (unsigned char)data[i + j];

//             // Format hex portion
//             static const char hexChars[] = "0123456789ABCDEF";
//             hexLine[j*3] = hexChars[c >> 4];
//             hexLine[j*3 + 1] = hexChars[c & 0xF];

//             // Format ASCII portion
//             asciiLine[j] = (c >= 32 && c <= 126) ? c : '.';
//         }

//         // Output the formatted line
//         TEST_Printf("%s  %s\n", hexLine, asciiLine);
//     }

//     // Try to interpret as a string if it looks like one
//     bool printable = true;
//     for (char c : data) {
//         if ((unsigned char)c < 32 || (unsigned char)c > 126) {
//             if (c != '\n' && c != '\r' && c != '\t') {
//                 printable = false;
//                 break;
//             }
//         }
//     }

//     if (printable) {
//         TEST_Printf("\nInterpretable as text:\n%s\n", data.c_str());
//     }
// }

void PrintHumanReadable(const std::string &data)
{
	// Print size information
	TEST_Printf("Received data: %zu bytes\n", data.size());

	// Hex output - 16 bytes per line
	TEST_Printf("Hex representation:\n");
	char hexLine[16 * 3 + 1]; // Each byte becomes 2 hex chars + 1 space, plus null terminator
	char asciiLine[17];		  // 16 chars + null terminator

	for (size_t i = 0; i < data.size(); i += 16)
	{
		// Format position
		TEST_Printf("%04zx: ", i);

		// Clear buffers
		memset(hexLine, ' ', sizeof(hexLine) - 1);
		hexLine[sizeof(hexLine) - 1] = 0;
		memset(asciiLine, 0, sizeof(asciiLine));

		// Process up to 16 bytes
		for (size_t j = 0; j < 16 && i + j < data.size(); j++)
		{
			unsigned char c = (unsigned char)data[i + j];

			// Format hex portion
			static const char hexChars[] = "0123456789ABCDEF";
			hexLine[j * 3] = hexChars[c >> 4];
			hexLine[j * 3 + 1] = hexChars[c & 0xF];

			// Format ASCII portion
			asciiLine[j] = (c >= 32 && c <= 126) ? c : '.';
		}

		// Output the formatted line
		TEST_Printf("%s  %s\n", hexLine, asciiLine);
	}

	// Try to interpret as a string if it looks like one
	bool printable = true;
	for (char c : data)
	{
		if ((unsigned char)c < 32 || (unsigned char)c > 126)
		{
			if (c != '\n' && c != '\r' && c != '\t')
			{
				printable = false;
				break;
			}
		}
	}

	if (printable)
	{
		TEST_Printf("\nInterpretable as text:\n%s\n", data.c_str());
	}

	// NEW: Enhanced human-readable analysis
	TEST_Printf("\n-------- ENHANCED READABLE FORMAT --------\n");

	// Check for WebRTC ICE candidates
	std::vector<std::string> candidates;
	std::string dataStr(data.begin(), data.end());

	// Extract ICE candidates
	size_t pos = 0;
	while ((pos = dataStr.find("candidate:", pos)) != std::string::npos)
	{
		size_t endPos = dataStr.find(" typ ", pos);
		if (endPos != std::string::npos)
		{
			endPos = dataStr.find_first_of("\r\n", endPos);
			if (endPos == std::string::npos)
			{
				endPos = dataStr.length();
			}
			candidates.push_back(dataStr.substr(pos, endPos - pos));
		}
		pos++;
	}

	// Print extracted ICE candidates in readable format
	if (!candidates.empty())
	{
		TEST_Printf("Found %zu WebRTC ICE candidates:\n", candidates.size());
		for (size_t i = 0; i < candidates.size(); i++)
		{
			std::string &candidate = candidates[i];

			// Extract fields from candidate
			std::istringstream ss(candidate);
			std::string token;
			std::vector<std::string> fields;

			while (std::getline(ss, token, ' '))
			{
				fields.push_back(token);
			}

			// Parse candidate fields if valid format
			if (fields.size() >= 8)
			{
				std::string candidateId;
				std::string protocol;
				std::string ip;
				std::string port;
				std::string type;

				// Extract ID
				size_t idPos = fields[0].find(':');
				if (idPos != std::string::npos)
				{
					candidateId = fields[0].substr(idPos + 1);
				}

				// Extract protocol, usually at position 2
				for (size_t j = 1; j < fields.size(); j++)
				{
					if (fields[j] == "udp" || fields[j] == "tcp")
					{
						protocol = fields[j];

						// For WebRTC ICE candidates, the format should be:
						// candidate:23683448 0 udp 2130704127 172.22.16.1 57720 typ host
						//                          ^ protocol  ^ priority  ^ IP   ^ port

						// IP address is typically at position j+2 (after protocol and priority)
						if (j + 2 < fields.size())
						{
							ip = fields[j + 2];
						}

						// Port is typically at position j+3 (after IP)
						if (j + 3 < fields.size())
						{
							port = fields[j + 3];
						}
						break;
					}
				}

				// Find type
				for (size_t j = 0; j < fields.size() - 1; j++)
				{
					if (fields[j] == "typ")
					{
						type = fields[j + 1];
						break;
					}
				}

				TEST_Printf("Candidate %d:\n", i + 1);
				TEST_Printf("  ID: %s\n", candidateId.c_str());
				TEST_Printf("  Protocol: %s\n", protocol.c_str());
				TEST_Printf("  IP: %s\n", ip.c_str());
				TEST_Printf("  Port: %s\n", port.c_str());
				TEST_Printf("  Type: %s\n", type.c_str());
				TEST_Printf("\n");
			}
			else
			{
				// Just print the raw candidate if parsing failed
				TEST_Printf("Candidate %d: %s\n\n", i + 1, candidate.c_str());
			}
		}
	}

	// Look for peer information
	std::vector<std::string> peerInfo;
	pos = 0;
	while ((pos = dataStr.find("peer_", pos)) != std::string::npos)
	{
		size_t endPos = dataStr.find_first_of("\r\n \t", pos);
		if (endPos == std::string::npos)
		{
			endPos = dataStr.length();
		}
		std::string peer = dataStr.substr(pos, endPos - pos);
		if (std::find(peerInfo.begin(), peerInfo.end(), peer) == peerInfo.end())
		{
			peerInfo.push_back(peer);
		}
		pos++;
	}

	if (!peerInfo.empty())
	{
		TEST_Printf("Peer Information:\n");
		for (const auto &peer : peerInfo)
		{
			TEST_Printf("  %s\n", peer.c_str());
		}
		TEST_Printf("\n");
	}

	// Print data type summary
	TEST_Printf("Data Summary:\n");
	if (!candidates.empty())
	{
		TEST_Printf("  - Contains WebRTC ICE candidates\n");
	}
	if (!peerInfo.empty())
	{
		TEST_Printf("  - Contains peer connection information\n");
	}

	// Additional protocol detection
	if (dataStr.find("SDP") != std::string::npos ||
		dataStr.find("m=audio") != std::string::npos ||
		dataStr.find("m=video") != std::string::npos)
	{
		TEST_Printf("  - Contains SDP (Session Description Protocol) data\n");
	}

	if (dataStr.find("STUN") != std::string::npos)
	{
		TEST_Printf("  - Contains STUN protocol data\n");
	}

	if (dataStr.find("TURN") != std::string::npos)
	{
		TEST_Printf("  - Contains TURN protocol data\n");
	}

	TEST_Printf("----------------------------------------\n");
}

void TrivialSignalingClient::Poll()
{
	// Drain the socket into the buffer, and check for reconnecting
	sockMutex.lock();
	// if (m_sock == INVALID_SOCKET)
	// {
	// 	Connect();
	// }
	// else
	{
		for (;;)
		{
			char buf[256];
			int r = recv(m_sock, buf, sizeof(buf), 0);
			if (r == 0)
				break;
			if (r < 0)
			{
				int e = GetSocketError();
				if (!IgnoreSocketError(e))
				{
					TEST_Printf("Failed to recv from trivial signaling server.  recv() returned %d, errno=%d.  Closing and restarting connection\n", r, e);
					CloseSocket();
				}
				break;
			}

			m_sBufferedData.append(buf, r);
		}
	}

	// Flush send queue
	if (m_sock != INVALID_SOCKET)
	{
		while (!m_queueSend.empty())
		{
			const std::string &s = m_queueSend.front();
			// TEST_Printf("Sending signal: '%s'\n", s.c_str());
			int l = (int)s.length();
			int r = ::send(m_sock, s.c_str(), l, 0);
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
				CloseSocket();
				break;
			}
		}
	}

	// sockMutex.unlock();
	// return;
	// Release the lock now.  See the notes below about why it's very important
	// to release the lock early and not hold it while we try to dispatch the
	// received callbacks.
	sockMutex.unlock();

	// Now dispatch any buffered signals
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
					goto next_message;
				}
				data.push_back((char)(dh << 4 | dl));
			}

			// Setup a context object that can respond if this signal is a connection request.
			struct Context : ISteamNetworkingSignalingRecvContext
			{
				TrivialSignalingClient *m_pOwner;

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
					return m_pOwner->CreateSignalingForConnection(identityPeer, ignoreErrMsg);
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
			m_pSteamNetworkingSockets->ReceivedP2PCustomSignal(data.c_str(), (int)data.length(), &context);
		}

	next_message:
		m_sBufferedData.erase(0, l + 1);
	}
}

void TrivialSignalingClient::Release()
{
	// NOTE: Here we are assuming that the calling code has already cleaned
	// up all the connections, to keep the example simple.
	CloseSocket();
}

std::unique_ptr<TrivialSignalingClient> TrivialSignalingClient::CreateTrivialSignalingClient(
	const char *pszServerAddress,					  // Address of the server.
	ISteamNetworkingSockets *pSteamNetworkingSockets, // Where should we send signals when we get them?
	SteamNetworkingErrMsg &errMsg					  // Error message is retjrned here if we fail
)
{

	std::string sAddress(pszServerAddress);
	std::string sService;
	size_t colon = sAddress.find(':');
	if (colon == std::string::npos)
	{
		sService = "10000"; // Default port
	}
	else
	{
		sService = sAddress.substr(colon + 1);
		sAddress.erase(colon);
	}

	// Resolve name synchronously
	addrinfo *pAddrInfo = nullptr;
	int r = getaddrinfo(sAddress.c_str(), sService.c_str(), nullptr, &pAddrInfo);
	if (r != 0 || pAddrInfo == nullptr)
	{
		sprintf(errMsg, "Invalid/unknown server address.  getaddrinfo returned %d", r);
		return nullptr;
	}

	// auto *pClient = new TrivialSignalingClient(pAddrInfo->ai_addr, pAddrInfo->ai_addrlen, pSteamNetworkingSockets);
	std::unique_ptr<TrivialSignalingClient> pClient = std::make_unique<TrivialSignalingClient>(pAddrInfo->ai_addr, pAddrInfo->ai_addrlen, pSteamNetworkingSockets, pszServerAddress);
	freeaddrinfo(pAddrInfo);

	return pClient;
}
