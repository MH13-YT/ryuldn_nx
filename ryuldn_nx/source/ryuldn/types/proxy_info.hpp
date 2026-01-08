#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Proxy routing information (16 bytes)
    // Matches Ryujinx LdnRyu/Types/ProxyInfo.cs
    struct ProxyInfo {
        u32 sourceIpV4;     // Source IPv4 address
        u16 sourcePort;     // Source port
        u32 destIpV4;       // Destination IPv4 address
        u16 destPort;       // Destination port
        u32 protocol;       // Protocol type (TCP=6, UDP=17)
    } __attribute__((packed));
    static_assert(sizeof(ProxyInfo) == 0x10, "ProxyInfo must be 0x10 bytes");

}
