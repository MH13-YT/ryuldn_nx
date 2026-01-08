#pragma once
#include <vapours.hpp>
#include "proxy_info.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Proxy data packet header
    // Matches Ryujinx LdnRyu/Types/ProxyDataHeader.cs
    struct ProxyDataHeaderFull {
        ProxyInfo info;         // Routing information (16 bytes)
        u32 dataLength;         // Length of data following this header
    } __attribute__((packed));

}
