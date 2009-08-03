#pragma once

#include <enet/enet.h>
#include <map>
#include "World.h"
#include "ClientInfo.h"
#include "Subject.h"
#include "Events.h"
#include "Stream.h"

class CServer : public CSubject<EventNewClientConnected>,
                public CSubject<EventClientDisconnected>
{
public:
	CServer(CWorld* world);
	~CServer(void);

	bool Create(int port); // Server an Port starten
	void Shutdown(); // Server herunterfahren

	void Update(const float dt);

protected:
	bool SendWorldToClient(CClientInfo* client);
	void OnReceive(CStream* stream, CClientInfo* client);

	void UpdateHistoryBuffer(); // Alte HistoryBuffer Eintr�ge l�schen, kein Client ben�tigt mehr so eine alte Welt, oder die Welt ist zu alt und Client bekommt ein komplettes Update.
	void ClientHistoryACK(CClientInfo* client, DWORD worldid); // Client best�tigt

private:
	ENetHost* m_server;
	std::map<int, CClientInfo*> m_clientlist;

	std::map<DWORD, world_state_t> m_history; // World History Buffer. Ben�tigt f�r Quake 3 Network Modell bzw. differentielle Updates an Clients

	DWORD m_lastupdate;
	CWorld* m_world;

  	CStream m_stream; // damit buffer nicht jedesmal neu erstellt werden muss
};
