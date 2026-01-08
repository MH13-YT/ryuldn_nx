/**
 * @file ryuldn_ipc.h
 * @brief IPC definitions shared between sysmodule and overlay
 * @copyright 2025
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define RYULDN_CONFIG_SERVICE_NAME "ryuldn:cfg"

/**
 * @brief States of the ryuldn_nx sysmodule (matches SessionState in types.hpp)
 */
typedef enum {
    RyuLdnState_None = 0,              ///< Not initialized
    RyuLdnState_Initialized = 1,       ///< Initialized, ready
    RyuLdnState_Scanning = 2,          ///< Scanning for networks
    RyuLdnState_HostCreating = 3,      ///< Creating access point
    RyuLdnState_HostActive = 4,        ///< Hosting a session
    RyuLdnState_ClientConnecting = 5,  ///< Connecting to network
    RyuLdnState_ClientConnected = 6,   ///< Connected to session
    RyuLdnState_Disconnecting = 7,     ///< Disconnecting
    RyuLdnState_Error = 8              ///< Error state
} RyuLdnState;

/**
 * @brief Complete status information
 */
typedef struct {
    RyuLdnState state;              ///< Current state
    bool game_running;              ///< An LDN game is running
    bool server_connected;          ///< Connected to master server
    bool in_session;                ///< In a game session
    uint32_t player_count;          ///< Number of players in session
    uint32_t max_players;           ///< Max players
    char session_name[33];          ///< Session name (SSID)
    uint64_t local_communication_id; ///< Game ID
    uint8_t node_id;                ///< Our position (0-7)
    uint32_t virtual_ip;            ///< Assigned virtual IP
    uint64_t bytes_sent;            ///< Network stats
    uint64_t bytes_received;        ///< Network stats
    int64_t ping_ms;                ///< Latency to server (-1 if N/A)
} RyuLdnStatus;

/**
 * @brief Configuration
 */
typedef struct {
    bool enabled;                   ///< Enable ryuLDN
    char server_ip[256];            ///< Server IP address
    uint16_t server_port;           ///< Server port
    char passphrase[128];           ///< Passphrase for private lobbies (format: Ryujinx-XXXXXXXX or empty)
    char username[33];              ///< Default username
} RyuLdnConfig;

/**
 * @brief IPC Command IDs (must match ryuldn_config_service.hpp)
 */
typedef enum {
    RyuLdnConfigCmd_GetStatus = 0,        ///< () -> RyuLdnStatus
    RyuLdnConfigCmd_GetConfig = 1,        ///< () -> RyuLdnConfig
    RyuLdnConfigCmd_SetConfig = 2,        ///< (RyuLdnConfig) -> Result
    RyuLdnConfigCmd_SetEnabled = 3,       ///< (u8 bool) -> Result
    RyuLdnConfigCmd_SetServerAddress = 4, ///< (InBuffer ip, u16 port) -> Result
    RyuLdnConfigCmd_SetPassphrase = 5,    ///< (InBuffer passphrase) -> Result (LOCKED if game active)
    RyuLdnConfigCmd_SetUsername = 6,      ///< (InBuffer username) -> Result
    RyuLdnConfigCmd_Reconnect = 7,        ///< () -> Result (reconnect to server)
    RyuLdnConfigCmd_ForceDisconnect = 8,  ///< () -> Result (force disconnect all)
} RyuLdnConfigCmd;

#ifdef __cplusplus
}
#endif
