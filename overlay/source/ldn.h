#pragma once
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Service s;
} LdnMitmConfigService;

Result ldnMitmSaveLogToFile(LdnMitmConfigService *s);
Result ldnMitmGetVersion(LdnMitmConfigService *s, char *version);
Result ldnMitmGetEnabled(LdnMitmConfigService *s, u32 *enabled);
Result ldnMitmSetEnabled(LdnMitmConfigService *s, u32 enabled);
Result ldnMitmGetLoggingEnabled(LdnMitmConfigService *s, u32 *enabled);
Result ldnMitmSetLoggingEnabled(LdnMitmConfigService *s, u32 enabled);
Result ldnMitmGetLoggingLevel(LdnMitmConfigService *s, u32 *level);
Result ldnMitmSetLoggingLevel(LdnMitmConfigService *s, u32 level); 
Result ldnMitmGetConfig(LdnMitmConfigService *out);
Result ldnMitmGetConfigFromService(Service* s, LdnMitmConfigService *out);

#ifdef __cplusplus
}
#endif
