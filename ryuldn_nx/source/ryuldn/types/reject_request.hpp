#pragma once
#include <vapours.hpp>
#include "disconnect_reason.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Reject connection request (8 bytes)
    // Matches Ryujinx LdnRyu/Types/RejectRequest.cs
    struct RejectRequest {
        u32 nodeId;                 // ID of node to reject
        DisconnectReason disconnectReason;
    } __attribute__((packed));
    static_assert(sizeof(RejectRequest) == 0x8, "RejectRequest must be 0x8 bytes");

}
