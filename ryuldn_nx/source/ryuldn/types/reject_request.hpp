#pragma once
#include <vapours.hpp>
#include "disconnect_reason.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Reject connection request
    // Matches Ryujinx LdnRyu/Types/RejectRequest.cs
    struct RejectRequest {
        DisconnectReason disconnectReason;
        u8 padding[3];
        u32 nodeId;     // ID of node to reject
    } __attribute__((packed));

}
