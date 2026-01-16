#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // External proxy authentication token
    // Matches Ryujinx LdnRyu/Types/ExternalProxyToken.cs
    // Size = 0x28 (40 bytes)
    struct ExternalProxyToken {
        u32 virtualIp;          // Virtual IP address assigned by master
        u8 token[16];           // 128-bit authentication token
        u8 physicalIp[16];      // Physical IP (IPv4 or IPv6)
        u32 addressFamily;      // Address family (2 = AF_INET, 23 = AF_INET6)
    } __attribute__((packed));
    static_assert(sizeof(ExternalProxyToken) == 0x28, "ExternalProxyToken must be 0x28 bytes");

}
