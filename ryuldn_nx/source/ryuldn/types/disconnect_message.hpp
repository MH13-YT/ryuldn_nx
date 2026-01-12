#pragma once
#include <vapours.hpp>
#include "disconnect_reason.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Disconnect notification message
    // Matches Ryujinx LdnRyu/Types/DisconnectMessage.cs (DisconnectIP)
    struct DisconnectMessage {
        u32 disconnectIp;    // IPv4 of the peer that disconnected
    } __attribute__((packed));
    static_assert(sizeof(DisconnectMessage) == 0x4, "DisconnectMessage must be 0x4 bytes");

}
