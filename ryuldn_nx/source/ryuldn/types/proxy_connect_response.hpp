#pragma once
#include <vapours.hpp>
#include "proxy_info.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Proxy TCP connection response
    // Matches Ryujinx LdnRyu/Types/ProxyConnectResponse.cs
    struct ProxyConnectResponseFull {
        ProxyInfo info;     // Connection routing information
        u32 result;         // 0 = success, non-zero = error
    } __attribute__((packed));

}
