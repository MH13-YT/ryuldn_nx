#include "ryuldn_ipc.hpp"
#include <cstring>
#include <switch.h>

/* ===== SERVICE INITIALIZATION ===== */

Result ryuldnGetConfigFromService(Service* ldnSrv, RyuLdnConfigService *out) {
    return serviceDispatch(ldnSrv, 65000,
        .out_num_objects = 1,
        .out_objects = &out->s
    );
}

/* ===== SIMPLIFIED IPC INTERFACE ===== */

Result ryuldnGetVersion(RyuLdnConfigService *srv, char *version) {
    RyuLdnVersion version_buf;
    Result rc = serviceDispatchOut(&srv->s, 65001, version_buf);
    if (R_SUCCEEDED(rc)) {
        strcpy(version, version_buf.raw);
    }
    return rc;
}

Result ryuldnGetStatus(RyuLdnConfigService *srv, RyuLdnStatus *status) {
    return serviceDispatchOut(&srv->s, 65002, *status);
}

Result ryuldnGetLogging(RyuLdnConfigService *srv, u32 *enabled) {
    return serviceDispatchOut(&srv->s, 65002, *enabled);
}

Result ryuldnSetLogging(RyuLdnConfigService *srv, u32 enabled) {
    return serviceDispatchIn(&srv->s, 65003, enabled);
}

Result ryuldnGetEnabled(RyuLdnConfigService *srv, u32 *enabled) {
    return serviceDispatchOut(&srv->s, 65004, *enabled);
}

Result ryuldnSetEnabled(RyuLdnConfigService *srv, u32 enabled) {
    return serviceDispatchIn(&srv->s, 65005, enabled);
}

Result ryuldnGetPassphrase(RyuLdnConfigService *srv, char *passphrase) {
    RyuLdnPassphrase pass_buf;
    Result rc = serviceDispatchOut(&srv->s, 65006, pass_buf);
    if (R_SUCCEEDED(rc)) {
        strcpy(passphrase, pass_buf.raw);
    }
    return rc;
}

Result ryuldnSetPassphrase(RyuLdnConfigService *srv, const char *passphrase) {
    RyuLdnPassphrase pass_buf;
    strcpy(pass_buf.raw, passphrase);
    return serviceDispatchIn(&srv->s, 65007, pass_buf);
}

Result ryuldnGetServerIP(RyuLdnConfigService *srv, char *server_ip) {
    RyuLdnServerIP ip_buf;
    Result rc = serviceDispatchOut(&srv->s, 65008, ip_buf);
    if (R_SUCCEEDED(rc)) {
        strcpy(server_ip, ip_buf.raw);
    }
    return rc;
}

Result ryuldnSetServerIP(RyuLdnConfigService *srv, const char *server_ip) {
    RyuLdnServerIP ip_buf;
    strcpy(ip_buf.raw, server_ip);
    return serviceDispatchIn(&srv->s, 65009, ip_buf);
}

Result ryuldnGetServerPort(RyuLdnConfigService *srv, u16 *port) {
    return serviceDispatchOut(&srv->s, 65010, *port);
}

Result ryuldnSetServerPort(RyuLdnConfigService *srv, u16 port) {
    return serviceDispatchIn(&srv->s, 65011, port);
}

Result ryuldnGetLoggingLevel(RyuLdnConfigService *srv, u32 *level) {
    return serviceDispatchOut(&srv->s, 65012, *level);
}

Result ryuldnSetLoggingLevel(RyuLdnConfigService *srv, u32 level) {
    return serviceDispatchIn(&srv->s, 65013, level);
}

void ryuldnConfigCleanup() {
    // Nothing to do - service will be closed by caller
}

