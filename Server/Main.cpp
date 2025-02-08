#include <iostream>
#include <unordered_map>
#include <thread>


#include "steam/steamnetworkingsockets.h"
#include "steam/steamnetworkingtypes.h"
#include "steam/isteamnetworkingutils.h"

#define IS_LOCAL_HOST true

struct Player {
	HSteamNetConnection connection;
	int posX;
	int posY;
	bool isIdle;
};

enum MessageType : uint8_t {
	INITIAL_CONNECTION = 1,
	PLAYER_POSITION_UPDATE = 2
};

std::unordered_map<HSteamNetConnection, Player> players;
ISteamNetworkingSockets* pInterface = nullptr;
HSteamListenSocket listenSocket = k_HSteamListenSocket_Invalid;
HSteamNetPollGroup pollGroup = k_HSteamNetPollGroup_Invalid;


void OnClientConnect(HSteamNetConnection conn)
{
	Player newPlayer;
	newPlayer.connection = conn;
	newPlayer.posX = 50;
	newPlayer.posY = 50;
	newPlayer.isIdle = true;

	players[conn] = newPlayer;
	std::cout << "New player connected: " << players[conn].connection << std::endl;
	std::cout << "Initializing their position to: " << players[conn].posX << ", " << players[conn].posY << std::endl;
	std::cout << "We have a total of " << players.size() << " players." << std::endl;

	// Create a message buffer with type identifier + player data
	std::vector<uint8_t> buffer;
	buffer.push_back(INITIAL_CONNECTION); // Add message type
	buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&newPlayer), reinterpret_cast<uint8_t*>(&newPlayer) + sizeof(Player));

	// Respond with their data
	pInterface->SendMessageToConnection(conn, buffer.data(), buffer.size(), k_nSteamNetworkingSend_Unreliable, nullptr);
}

void OnClientDisconnected(HSteamNetConnection conn)
{
	if (players.find(conn) != players.end())
	{
		players.erase(conn);
		printf("Player disconnected: %d\n", conn);
	}
}

void PollIncomingMessages()
{
	ISteamNetworkingMessage* messages[10];
	int numMessages = pInterface->ReceiveMessagesOnPollGroup(pollGroup, messages, 10);

	for (int i = 0; i < numMessages; ++i)
	{
		ISteamNetworkingMessage* msg = messages[i];

		HSteamNetConnection conn = msg->m_conn;

		// Process the message
		// Cast msg->m_pData to uint8_t* for safe pointer arithmetic
		uint8_t* data = static_cast<uint8_t*>(msg->m_pData);
		uint8_t messageType = data[0]; // Read first byte as message type

		if (messageType == PLAYER_POSITION_UPDATE) {
			// Process Player updates
			const Player* receivedPlayer = reinterpret_cast<const Player*>(data + 1);
			if (players.find(receivedPlayer->connection) != players.end())
			{
				players[receivedPlayer->connection].posX = receivedPlayer->posX;
				players[receivedPlayer->connection].posY = receivedPlayer->posY;
				players[receivedPlayer->connection].isIdle = receivedPlayer->isIdle;
				std::cout << "Received update from " << receivedPlayer->connection << std::endl;
			}
			else
			{
				std::cout << "[ERROR] Player not initialized!!" << std::endl;
			}
		}
		else {
			std::cerr << "Unknown message type received!" << std::endl;
		}

		msg->Release();
	}
}

void BroadcastPlayerData()
{
	if (players.empty()) return;


	std::vector<uint8_t> buffer;
	buffer.push_back(PLAYER_POSITION_UPDATE); // Add message type

	for (const auto& player : players)
	{
		const Player& p = player.second;
		buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&p), reinterpret_cast<const uint8_t*>(&p) + sizeof(Player));
	}

	size_t payloadSize = buffer.size();

	for (const auto& player : players) {
		pInterface->SendMessageToConnection(player.first, buffer.data(), payloadSize, k_nSteamNetworkingSend_Unreliable, nullptr);
	}
}

void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
{

	switch (info->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_Connecting:
		std::cout << "New player attempting to connect: " << info->m_hConn << std::endl;

		// Accept the connection and add to the poll group
		pInterface->AcceptConnection(info->m_hConn);
		pInterface->SetConnectionPollGroup(info->m_hConn, pollGroup);

		// Store connection
		OnClientConnect(info->m_hConn);
		break;
	case k_ESteamNetworkingConnectionState_ClosedByPeer:
		OnClientConnect(info->m_hConn);
		pInterface->CloseConnection(info->m_hConn, 0, nullptr, false);
		break;
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		// Handle disconnects
		OnClientDisconnected(info->m_hConn);
		pInterface->CloseConnection(info->m_hConn, 0, nullptr, false);
		break;
	}
}

int main()
{

	// Init the library
	SteamDatagramErrMsg errMsg;
	if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
		fprintf(stderr, "GameNetworkingSockets_Init failed. %s\n", errMsg);
		exit(1);
	}

	// Register the callback
	SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(OnConnectionStatusChanged);

	// ------------------ Create the server socket (START) -----------------------
	pInterface = SteamNetworkingSockets();
	
	SteamNetworkingIPAddr serverAddr;
	serverAddr.Clear();

#ifdef IS_LOCAL_HOST
	serverAddr.ParseString("127.0.0.1:7777");
#else
	serverAddr.SetIPv4(0x7F000001, 7777); // 0.0.0.0:7777
#endif

	std::cout <<"Is local host? " << serverAddr.IsLocalHost() << std::endl;

	listenSocket = pInterface->CreateListenSocketIP(serverAddr, 0, nullptr);
	if (listenSocket == k_HSteamListenSocket_Invalid) {
		fprintf(stderr, "Failed to create listen socket\n");
		exit(1);
	}
	// ------------------ Create the server socket (END) -----------------------



	// ------------------ Optimize the connection (START) -----------------------

	int32_t configUnencrypted = 1;
	SteamNetworkingUtils()->SetConfigValue(
		k_ESteamNetworkingConfig_Unencrypted,
		k_ESteamNetworkingConfig_Global,
		0,
		k_ESteamNetworkingConfig_Int32,
		&configUnencrypted
	);
	
	int32_t mtuPacketSize = 1200;
	SteamNetworkingUtils()->SetConfigValue(
		k_ESteamNetworkingConfig_MTU_PacketSize,
		k_ESteamNetworkingConfig_Global,
		0,
		k_ESteamNetworkingConfig_Int32,
		&mtuPacketSize
	);

	int32_t sendRateMax = 1000000;
	SteamNetworkingUtils()->SetConfigValue(
		k_ESteamNetworkingConfig_SendRateMax,
		k_ESteamNetworkingConfig_Global,
		0,
		k_ESteamNetworkingConfig_Int32,
		&sendRateMax // 1 Mbps max
	);

	int32_t allowWithoutAuth = 1;
	SteamNetworkingUtils()->SetConfigValue(
		k_ESteamNetworkingConfig_IP_AllowWithoutAuth,
		k_ESteamNetworkingConfig_Global,
		0,
		k_ESteamNetworkingConfig_Int32,
		&allowWithoutAuth
	);

	float fakePacketLossSend = 0;
	SteamNetworkingUtils()->SetConfigValue(
		k_ESteamNetworkingConfig_FakePacketLoss_Send,
		k_ESteamNetworkingConfig_Global,
		0,
		k_ESteamNetworkingConfig_Float,
		&fakePacketLossSend // 0% fake packet loss
	);

	// Create a poll group to manage connections
	pollGroup = pInterface->CreatePollGroup();
	if (pollGroup == k_HSteamNetPollGroup_Invalid)
	{
		std::cerr << "Failed to create poll group!" << std::endl;
		return -1;
	}


	std::cout << "Server Created..." << std::endl;

	// ------------------ Optimize the connection (END) -----------------------


	while (true)
	{
		SteamNetworkingSockets()->RunCallbacks();
		PollIncomingMessages();
		BroadcastPlayerData();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}


	// ----------------- CleanUp (START) ---------------------------------------
	pInterface->CloseListenSocket(listenSocket);
	GameNetworkingSockets_Kill();

	// ----------------- CleanUp (END) --------------------------------------- 


}