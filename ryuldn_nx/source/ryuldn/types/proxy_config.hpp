#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Proxy network configuration (8 bytes)
    // Matches Ryujinx Network/Types/ProxyConfig.cs
    struct ProxyConfig {
        u32 proxyIp;            // Virtual IP address for LAN proxy
        u32 proxySubnetMask;    // Subnet mask for virtual network
    } __attribute__((packed));
    static_assert(sizeof(ProxyConfig) == 0x8, "ProxyConfig must be 0x8 bytes");

}
