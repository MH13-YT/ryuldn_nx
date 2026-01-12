#pragma once

#include "state.hpp"
#include "../ryuldn_ipc.hpp"

/* ===== SERVICE INITIALIZATION ===== */

/**
 * Initialize ldn:u service and configuration
 * @return true if successful, false if error
 */
bool serviceInitialize();

/**
 * Close all service handles
 */
void serviceShutdown();

/* ===== GLOBAL CONFIG CACHE ===== */
extern Service g_ldnSrv;
extern RyuLdnConfigService g_configSrv;
extern RyuLdnConfig g_config;
extern RyuLdnStatus g_status;
