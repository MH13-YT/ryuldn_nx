#pragma once
#include <stratosphere.hpp>

namespace ams::mitm::ldn {

    struct LdnMitmVersion {
        char raw[32];
    };

}

#define AMS_LDN_CONFIG(C, H)                                                                								   \
    AMS_SF_METHOD_INFO(C, H, 65001, Result, GetVersion, 	(ams::sf::Out<ams::mitm::ldn::LdnMitmVersion> version), (version)) \
    AMS_SF_METHOD_INFO(C, H, 65002, Result, GetEnabled, 	(ams::sf::Out<u32> enabled), 							(enabled)) \
    AMS_SF_METHOD_INFO(C, H, 65003, Result, SetEnabled, 	(u32 enabled), 											(enabled)) \
    AMS_SF_METHOD_INFO(C, H, 65004, Result, GetLoggerEnabled, (ams::sf::Out<u32> logging),                        (logging)) \
    AMS_SF_METHOD_INFO(C, H, 65005, Result, SetLoggerEnabled, (u32 logging),                                      (logging)) \
    AMS_SF_METHOD_INFO(C, H, 65006, Result, GetLoggerLevel, (ams::sf::Out<u32> level),                             (level))   \
    AMS_SF_METHOD_INFO(C, H, 65007, Result, SetLoggerLevel, (u32 level),                                           (level))    \

    AMS_SF_DEFINE_INTERFACE(ams::mitm::ldn, ILdnConfig, AMS_LDN_CONFIG, 0x14c8af2c)
