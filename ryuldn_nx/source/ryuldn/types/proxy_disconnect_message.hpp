#pragma once
#include <vapours.hpp>
#include "proxy_info.hpp"
#include "disconnect_reason.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Proxy TCP disconnection notification (20 bytes)
    // Matches Ryujinx LdnRyu/Types/ProxyDisconnectMessage.cs
    struct ProxyDisconnectMessageFull {
        ProxyInfo info;             // Connection routing information
        DisconnectReason reason;    // Reason for disconnection
    } __attribute__((packed));
    static_assert(sizeof(ProxyDisconnectMessageFull) == 0x14, "ProxyDisconnectMessageFull must be 0x14 bytes");

}
