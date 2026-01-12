#pragma once
#include <stratosphere.hpp>
#include "../ryuldnnx_ipc_types.hpp"

namespace ams::mitm::ldn {

    struct RyuLdnVersion {
        char raw[32];
    };

    struct RyuLdnPassphrase {
        char raw[17];  // Format: "Ryujinx-xxxxxxxx" (16 chars + null)
    };

    struct RyuLdnServerIP {
        char raw[16];
    };

}

#define AMS_LDN_CONFIG(C, H)                                                                                               \
    AMS_SF_METHOD_INFO(C, H, 65001, Result, GetVersion,         (::ams::sf::Out<ams::mitm::ldn::RyuLdnVersion> version), (version))   \
    AMS_SF_METHOD_INFO(C, H, 65002, Result, GetLogging,         (::ams::sf::Out<u32> enabled),                           (enabled))    \
    AMS_SF_METHOD_INFO(C, H, 65003, Result, SetLogging,         (u32 enabled),                                           (enabled))    \
    AMS_SF_METHOD_INFO(C, H, 65004, Result, GetEnabled,         (::ams::sf::Out<u32> enabled),                           (enabled))    \
    AMS_SF_METHOD_INFO(C, H, 65005, Result, SetEnabled,         (u32 enabled),                                           (enabled))    \
    AMS_SF_METHOD_INFO(C, H, 65006, Result, GetPassphrase,      (::ams::sf::Out<ams::mitm::ldn::RyuLdnPassphrase> pass), (pass))      \
    AMS_SF_METHOD_INFO(C, H, 65007, Result, SetPassphrase,      (const ams::mitm::ldn::RyuLdnPassphrase& pass),          (pass))      \
    AMS_SF_METHOD_INFO(C, H, 65008, Result, GetServerIP,        (::ams::sf::Out<ams::mitm::ldn::RyuLdnServerIP> ip),     (ip))        \
    AMS_SF_METHOD_INFO(C, H, 65009, Result, SetServerIP,        (const ams::mitm::ldn::RyuLdnServerIP& ip),              (ip))        \
    AMS_SF_METHOD_INFO(C, H, 65010, Result, GetServerPort,      (::ams::sf::Out<u16> port),                              (port))       \
    AMS_SF_METHOD_INFO(C, H, 65011, Result, SetServerPort,      (u16 port),                                              (port))        \
    AMS_SF_METHOD_INFO(C, H, 65012, Result, GetLoggingLevel,    (::ams::sf::Out<u32> level),                             (level))       \
    AMS_SF_METHOD_INFO(C, H, 65013, Result, SetLoggingLevel,    (u32 level),                                             (level))

AMS_SF_DEFINE_INTERFACE(ams::mitm::ldn, ILdnConfig, AMS_LDN_CONFIG, 0x14c8af2c)
