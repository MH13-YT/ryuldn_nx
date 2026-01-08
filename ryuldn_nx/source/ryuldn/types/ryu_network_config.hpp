#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // RyuLDN-specific network configuration (40 bytes)
    // Matches Ryujinx LdnRyu/Types/RyuNetworkConfig.cs
    struct RyuNetworkConfig {
        u8 gameVersion[16];         // Game version identifier
        u8 privateIp[16];           // Private IP address (IPv4 or IPv6)
        u32 addressFamily;          // AddressFamily enum (InterNetwork=2, InterNetworkV6=23)
        u16 externalProxyPort;      // External P2P proxy port
        u16 internalProxyPort;      // Internal proxy port
    } __attribute__((packed));
    static_assert(sizeof(RyuNetworkConfig) == 0x28, "RyuNetworkConfig must be 0x28 bytes");

}
