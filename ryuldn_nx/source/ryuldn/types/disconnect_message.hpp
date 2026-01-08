#pragma once
#include <vapours.hpp>
#include "disconnect_reason.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Disconnect notification message
    // Matches Ryujinx LdnRyu/Types/DisconnectMessage.cs
    struct DisconnectMessage {
        DisconnectReason reason;
        u8 padding[3];
    } __attribute__((packed));

}
