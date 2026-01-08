#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Initialize message (22 bytes)
    // Matches Ryujinx LdnRyu/Types/InitializeMessage.cs
    struct InitializeMessage {
        u8 id[16];              // Client unique ID
        u8 macAddress[6];       // MAC address
    } __attribute__((packed));
    static_assert(sizeof(InitializeMessage) == 0x16, "InitializeMessage must be 0x16 bytes");

}
