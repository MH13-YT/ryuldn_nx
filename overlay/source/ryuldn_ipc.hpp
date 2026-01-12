#pragma once

#include <switch.h>
#include <cstdint>
#include <cstddef>

// Local copy of IPC structs (no shared header)
struct RyuLdnVersion {
    char raw[32];
};

struct RyuLdnPassphrase {
    char raw[17];  // Format: "Ryujinx-xxxxxxxx" (16 chars + null)
};

struct RyuLdnServerIP {
    char raw[16];
};

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

struct RyuLdnConfig {
    uint32_t enabled;  // 0=disabled, 1=enabled
    char server_ip[16];
    uint16_t server_port;
    char passphrase[17];  // Format: "Ryujinx-xxxxxxxx" (16 chars + null)
    char _reserved[33];     // Unused (was username - games provide it automatically)
};

// Command IDs for config service (from sysmodule)
enum RyuLdnConfigCmd : uint32_t {
    RyuLdnConfigCmd_GetVersion       = 65001,
    RyuLdnConfigCmd_GetLogging       = 65002,
    RyuLdnConfigCmd_SetLogging       = 65003,
    RyuLdnConfigCmd_GetEnabled       = 65004,
    RyuLdnConfigCmd_SetEnabled       = 65005,
    RyuLdnConfigCmd_GetPassphrase    = 65006,
    RyuLdnConfigCmd_SetPassphrase    = 65007,
    RyuLdnConfigCmd_GetServerIP      = 65008,
    RyuLdnConfigCmd_SetServerIP      = 65009,
    RyuLdnConfigCmd_GetServerPort    = 65010,
    RyuLdnConfigCmd_SetServerPort    = 65011,
    RyuLdnConfigCmd_GetLoggingLevel  = 65012,
    RyuLdnConfigCmd_SetLoggingLevel  = 65013,
};

// Validate struct sizes for IPC consistency
static_assert(sizeof(RyuLdnStatus) == 96, "RyuLdnStatus size mismatch");
static_assert(sizeof(RyuLdnConfig) == 72, "RyuLdnConfig size mismatch");
static_assert(sizeof(RyuLdnVersion) == 32, "RyuLdnVersion size mismatch");
static_assert(sizeof(RyuLdnPassphrase) == 17, "RyuLdnPassphrase size mismatch");
static_assert(sizeof(RyuLdnServerIP) == 16, "RyuLdnServerIP size mismatch");


/* ===== RYULDN CONFIG SERVICE ===== */
struct RyuLdnConfigService {
    Service s;
};

/* ===== Simplified IPC Interface ===== */

// Get config service from ldn:u (cmd 65000)
Result ryuldnGetConfigFromService(Service* ldnSrv, RyuLdnConfigService *out);

Result ryuldnGetVersion(RyuLdnConfigService *srv, char *version);
Result ryuldnGetStatus(RyuLdnConfigService *srv, RyuLdnStatus *status);
Result ryuldnGetLogging(RyuLdnConfigService *srv, u32 *enabled);
Result ryuldnSetLogging(RyuLdnConfigService *srv, u32 enabled);
Result ryuldnGetEnabled(RyuLdnConfigService *srv, u32 *enabled);
Result ryuldnSetEnabled(RyuLdnConfigService *srv, u32 enabled);
Result ryuldnGetPassphrase(RyuLdnConfigService *srv, char *passphrase);
Result ryuldnSetPassphrase(RyuLdnConfigService *srv, const char *passphrase);
Result ryuldnGetServerIP(RyuLdnConfigService *srv, char *server_ip);
Result ryuldnSetServerIP(RyuLdnConfigService *srv, const char *server_ip);
Result ryuldnGetServerPort(RyuLdnConfigService *srv, u16 *port);
Result ryuldnSetServerPort(RyuLdnConfigService *srv, u16 port);
Result ryuldnGetLoggingLevel(RyuLdnConfigService *srv, u32 *level);
Result ryuldnSetLoggingLevel(RyuLdnConfigService *srv, u32 level);

// Cleanup
void ryuldnConfigCleanup();
