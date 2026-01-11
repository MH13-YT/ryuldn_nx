#include <switch.h>
#include <string.h>
#include "ldn.h"

extern Service g_ldnSrv;

Result ldnMitmGetConfigFromService(Service* s, LdnMitmConfigService *out) {
    memcpy(&out->s, s, sizeof(Service));
    return 0;
}

Result ldnMitmGetConfig(LdnMitmConfigService *out) {
    return ldnMitmGetConfigFromService(&g_ldnSrv, out);
}

// GetVersion: Out<string> → tmp buffer
Result ldnMitmGetVersion(LdnMitmConfigService *s, char *version) {
    char tmp[32];
    Result rc = serviceDispatchOut(&s->s, 65001, tmp);
    if (R_SUCCEEDED(rc)) strcpy(version, tmp);
    return rc;
}

// GetXXX: Out<u32> → tmp + copy
Result ldnMitmGetEnabled(LdnMitmConfigService *s, u32 *enabled) {
    u32 tmp;
    Result rc = serviceDispatchOut(&s->s, 65002, tmp);
    if (R_SUCCEEDED(rc)) *enabled = tmp;
    return rc;
}

Result ldnMitmGetLoggingEnabled(LdnMitmConfigService *s, u32 *enabled) {
    u32 tmp;
    Result rc = serviceDispatchOut(&s->s, 65004, tmp);
    if (R_SUCCEEDED(rc)) *enabled = tmp;
    return rc;
}

Result ldnMitmGetLoggingLevel(LdnMitmConfigService *s, u32 *level) {
    u32 tmp = 0;
    Result rc = serviceDispatchOut(&s->s, 65006, tmp);  // ← 65006
    if (R_SUCCEEDED(rc)) {
        *level = tmp;
        return rc;
    }
    return rc;  // ← ajouté ici
}

// SetXXX: In<u32>
Result ldnMitmSetEnabled(LdnMitmConfigService *s, u32 enabled) {
    return serviceDispatchIn(&s->s, 65003, enabled);
}

Result ldnMitmSetLoggingEnabled(LdnMitmConfigService *s, u32 enabled) {
    return serviceDispatchIn(&s->s, 65005, enabled);
}

Result ldnMitmSetLoggingLevel(LdnMitmConfigService *s, u32 level) {
    Result rc = serviceDispatchIn(&s->s, 65007, level);  // ← 65007
    return rc;
}
