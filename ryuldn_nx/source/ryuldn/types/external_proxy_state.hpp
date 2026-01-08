#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // External proxy connection state
    // Matches Ryujinx LdnRyu/Types/ExternalProxyConnectionState.cs
    // Size = 0x8, Pack = 4
    struct ExternalProxyConnectionState {
        u32 ipAddress;          // IP address of the client
        bool connected;         // Connection state
        u8 padding[3];
    } __attribute__((packed));

}
