#include "service.hpp"
#include "../ryuldn_ipc.hpp"
#include <cstring>
#include <cstdio>

/* Global config cache */
extern Service g_ldnSrv;
RyuLdnConfigService g_configSrv = {0};
RyuLdnConfig g_config = {0};
RyuLdnStatus g_status = {static_cast<RyuLdnState>(0)};
char g_versionBuffer[64] = {0};
const char* g_version = g_versionBuffer;

/* Global error message */
char g_errorMessage[64] = {0};

bool serviceInitialize() {
    g_state = State::Uninit;

    Result rc;

    // Connect to ldn:u - when mitm is active, we get the mitm service
    rc = smGetService(&g_ldnSrv, "ldn:u");
    if (R_FAILED(rc)) {
        g_state = State::Error;
        snprintf(g_errorMessage, sizeof(g_errorMessage), "Cannot connect to ldn:u (0x%X)", rc);
        return false;
    }

    // Get the config service (cmd 65000) from ldn:u
    rc = ryuldnGetConfigFromService(&g_ldnSrv, &g_configSrv);
    if (R_FAILED(rc)) {
        g_state = State::Error;
        snprintf(g_errorMessage, sizeof(g_errorMessage), "RyuLDN config unavailable (0x%X)", rc);
        serviceClose(&g_ldnSrv);
        return false;
    }

    // Get version - test IPC connection
    Result ver_rc = ryuldnGetVersion(&g_configSrv, g_versionBuffer);
    if (R_FAILED(ver_rc)) {
        g_state = State::Error;
        snprintf(g_errorMessage, sizeof(g_errorMessage), "IPC communication failed (0x%X)", ver_rc);
        return false;
    }

    // Get enabled state - should succeed if IPC is working
    u32 enabled = 1;  // Default to enabled
    ryuldnGetEnabled(&g_configSrv, &enabled);
    g_config.enabled = enabled;

    // Get passphrase - optional, doesn't block boot
    char passphrase[128] = {0};
    ryuldnGetPassphrase(&g_configSrv, passphrase);
    strcpy(g_config.passphrase, passphrase);

    // Get server IP - optional, doesn't block boot
    char server_ip[256] = {0};
    ryuldnGetServerIP(&g_configSrv, server_ip);
    strcpy(g_config.server_ip, server_ip);

    // Get server port - optional, doesn't block boot
    u16 port = 11452;  // Default port
    ryuldnGetServerPort(&g_configSrv, &port);
    g_config.server_port = port;

    g_state = State::Loaded;
    return true;
}

void serviceShutdown() {
    if (g_configSrv.s.session != 0) {
        serviceClose(&g_configSrv.s);
    }
    if (g_ldnSrv.session != 0) {
        serviceClose(&g_ldnSrv);
    }
}
