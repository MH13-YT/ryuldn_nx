#pragma once
#include <vapours.hpp>
#include "proxy_info.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Proxy TCP connection request
    // Matches Ryujinx LdnRyu/Types/ProxyConnectRequest.cs
    struct ProxyConnectRequestFull {
        ProxyInfo info;     // Connection routing information
    } __attribute__((packed));

}
