#include <iostream>
#include <unordered_map>
#include <thread>

#include "steam/steamnetworkingsockets.h"
#include "steam/steamnetworkingtypes.h"
#include "steam/isteamnetworkingutils.h"

#define SERVER_IP "127.0.0.1:7777"

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

//TEMP
Player localPlayer;
//ENDTEMP

ISteamNetworkingSockets* pInterface = nullptr;
HSteamNetConnection connection = k_HSteamNetConnection_Invalid;

void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
{
	if (info->m_hConn != connection)
		return;

	switch (info->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_Connected:
		std::cout << "Connected to server!\n";
		break;

	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		std::cerr << "Disconnected from server!\n";
		connection = k_HSteamNetConnection_Invalid;
		break;
	}
}

void SendPlayerData()
{
	std::vector<uint8_t> buffer;
	buffer.push_back(PLAYER_POSITION_UPDATE); // Add message type

	buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&localPlayer), reinterpret_cast<const uint8_t*>(&localPlayer) + sizeof(Player));

	pInterface->SendMessageToConnection(connection, buffer.data(), buffer.size(), k_nSteamNetworkingSend_Unreliable, nullptr);
}

void ProcessIncomingMessages() {
	ISteamNetworkingMessage* messages[10]; // Array to hold messages
	int numMessages = SteamNetworkingSockets()->ReceiveMessagesOnConnection(connection, messages, 10);

	for (int i = 0; i < numMessages; i++) {
		ISteamNetworkingMessage* msg = messages[i];

		// Process the message
		// Cast msg->m_pData to uint8_t* for safe pointer arithmetic
		uint8_t* data = static_cast<uint8_t*>(msg->m_pData);
		uint8_t messageType = data[0]; // Read first byte as message type

		if (messageType == INITIAL_CONNECTION) {

			

			// Extract the Player data
			std::memcpy(&localPlayer, data + 1, sizeof(Player));

			std::cout << "Received initial connection response! Player ID: " << localPlayer.connection << std::endl;
		}
		else if (messageType == PLAYER_POSITION_UPDATE) {
			// Process multiple Player updates
			size_t numPlayers = (msg->m_cbSize - 1) / sizeof(Player);

			std::cout << "Received broadcast update for " << numPlayers << " players." << std::endl;

			const Player* receivedPlayers = reinterpret_cast<const Player*>(data + 1);
			for (size_t j = 0; j < numPlayers; j++) {
				std::cout << "Player " << receivedPlayers[j].connection << " Position: ("
					<< receivedPlayers[j].posX << ", "
					<< receivedPlayers[j].posY << ")" << std::endl;
			}
		}
		else {
			std::cerr << "Unknown message type received!" << std::endl;
		}

		// Release message memory
		msg->Release();
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

	// ------------------ Create the connection (START) -----------------------
	pInterface = SteamNetworkingSockets();
	SteamNetworkingIPAddr ipAddr;
	ipAddr.Clear();
	ipAddr.ParseString(SERVER_IP);

	SteamNetworkingConfigValue_t opts[2];
	opts[0].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
	opts[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, 4096); // Small buffer
	std::cout << "Attempting to connect the server... " << std::endl;
	connection = pInterface->ConnectByIPAddress(ipAddr, 1, opts);
	// -----------------------(END)--------------------------

	if (connection == k_HSteamNetConnection_Invalid)
	{
		printf("Could not connect to server");
	}
	else
	{
		std::cout << "Connection success: " << connection << std::endl;
	}

	while (true)
	{
		SteamNetworkingSockets()->RunCallbacks();
		ProcessIncomingMessages();
		SendPlayerData();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

}