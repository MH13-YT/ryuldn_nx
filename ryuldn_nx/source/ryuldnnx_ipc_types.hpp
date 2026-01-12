#pragma once
#include <cstdint>
#include <cstddef>
#include <stratosphere.hpp>

// IPC types for the ryuldn_nx config service (sysmodule side)

// States of the ryuldn_nx sysmodule
enum class RyuLdnState : uint32_t {
    None = 0,
    Initialized = 1,
    Scanning = 2,
    HostCreating = 3,
    HostActive = 4,
    ClientConnecting = 5,
    ClientConnected = 6,
    Disconnecting = 7,
    Error = 8,
};

// Complete status information
struct RyuLdnStatus {
    RyuLdnState state;
    bool game_running;
    bool server_connected;
    bool in_session;
    uint32_t player_count;
    uint32_t max_players;
    char session_name[33];
    uint64_t local_communication_id;
    uint8_t node_id;
    uint32_t virtual_ip;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    int64_t ping_ms;
};

// Configuration payload
struct RyuLdnConfig {
    uint32_t enabled;  // 0=disabled, 1=enabled
    char server_ip[16];
    uint16_t server_port;
    char passphrase[17];  // Format: "Ryujinx-xxxxxxxx" (16 chars + null)
    char _reserved[33];     // Unused (was username - games provide it automatically)
};

// Command IDs for the config service (ldn:u -> config object)
enum RyuLdnConfigCmd : uint32_t {
    RyuLdnConfigCmd_GetVersion      = 65001,
    RyuLdnConfigCmd_GetStatus       = 65002,
    RyuLdnConfigCmd_GetConfig       = 65003,
    RyuLdnConfigCmd_SetConfig       = 65004,
    RyuLdnConfigCmd_SetEnabled      = 65005,
    RyuLdnConfigCmd_SetServerAddress= 65006,
    RyuLdnConfigCmd_SetPassphrase   = 65007,
    // 65008 removed (was SetUsername - unused, games provide username automatically)
    RyuLdnConfigCmd_Reconnect       = 65009,
    RyuLdnConfigCmd_ForceDisconnect = 65010,
};
