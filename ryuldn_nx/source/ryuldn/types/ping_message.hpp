#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Ping/Pong message
    // Matches Ryujinx LdnRyu/Types/PingMessage.cs
    struct PingMessage {
        u32 requester;      // 0 = server request, 1 = client request
        u32 timestamp;      // Timestamp for RTT calculation
    } __attribute__((packed));

}
