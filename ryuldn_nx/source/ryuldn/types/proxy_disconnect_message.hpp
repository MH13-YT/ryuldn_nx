#pragma once
#include <vapours.hpp>
#include "proxy_info.hpp"
#include "disconnect_reason.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Proxy TCP disconnection notification
    // Matches Ryujinx LdnRyu/Types/ProxyDisconnectMessage.cs
    struct ProxyDisconnectMessageFull {
        ProxyInfo info;             // Connection routing information
        DisconnectReason reason;    // Reason for disconnection
        u8 padding[3];
    } __attribute__((packed));

}
