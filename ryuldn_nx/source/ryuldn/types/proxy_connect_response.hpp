#pragma once
#include <vapours.hpp>
#include "proxy_info.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Proxy TCP connection response
    // Matches Ryujinx LdnRyu/Types/ProxyConnectResponse.cs (16 bytes)
    struct ProxyConnectResponseFull {
        ProxyInfo info;     // Connection routing information
    } __attribute__((packed));
    static_assert(sizeof(ProxyConnectResponseFull) == 0x10, "ProxyConnectResponseFull must be 0x10 bytes");

}
