#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // External P2P proxy configuration
    // Matches Ryujinx LdnRyu/Types/ExternalProxyConfig.cs
    struct ExternalProxyConfig {
        u8 proxyIp[16];         // Proxy IP address (IPv4 or IPv6)
        u32 addressFamily;      // AddressFamily enum
        u16 proxyPort;          // Proxy port
        u8 token[16];           // Authentication token
    } __attribute__((packed));

}
