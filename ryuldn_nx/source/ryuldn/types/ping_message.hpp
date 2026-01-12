#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Ping/Pong message (2 bytes)
    // Matches Ryujinx LdnRyu/Types/PingMessage.cs
    struct PingMessage {
        u8 requester;   // 0 = server request, 1 = client request
        u8 id;          // Ping identifier
    } __attribute__((packed));
    static_assert(sizeof(PingMessage) == 0x2, "PingMessage must be 0x2 bytes");

}
