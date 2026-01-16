#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // External proxy connection state
    // Matches Ryujinx LdnRyu/Types/ExternalProxyConnectionState.cs
    // C# Pack = 4 means: align fields on 4-byte boundaries
    // Layout: ipAddress(4) @ 0-3, connected(1) @ 4, padding(3) @ 5-7
    struct ExternalProxyConnectionState {
        u32 ipAddress;          // IP address of the client (offset 0-3)
        bool connected;         // Connection state (offset 4)
        u8 padding[3];          // Padding to align to 4 bytes (offset 5-7)
    } __attribute__((packed));
    static_assert(sizeof(ExternalProxyConnectionState) == 0x8, "ExternalProxyConnectionState must be 0x8 bytes (8)");

}
