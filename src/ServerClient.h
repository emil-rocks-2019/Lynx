#pragma once

#define USE_RANGE_ENCODER       // use the enet compression mechanism

#define SV_MAX_CHALLENGE_TIME   6000                   // a client has X ms after the connection to send the challenge msg
#define MAX_SV_PACKETLEN        (USHRT_MAX)
#define MAX_CL_PACKETLEN        (1024)

#define MAX_SV_CL_POS_DIFF      (45.0f*45.0f)          // Distance squared
#define SERVER_UPDATETIME       (50)                   // Server sends a snapshot to the clients every $SERVER_UPDATETIME ms
#define RENDER_DELAY            (2*SERVER_UPDATETIME)  // Render delay in ms
#define MAX_CLIENT_HISTORY      (20*SERVER_UPDATETIME)

#define MAXCLIENTS              (8)
#define SERVER_MAX_WORLD_AGE    (6000)                 // Max. age of a server world snapshot in ms
#define MAX_WORLD_BACKLOG       (10*MAXCLIENTS)        // How many world snapshots does the server keep

#define OUTGOING_BANDWIDTH      (1024*15)              // bytes/sec
#define CLIENT_UPDATERATE       (50)                   // The clients sends an update every $CLIENT_UPDATERATE to the server
