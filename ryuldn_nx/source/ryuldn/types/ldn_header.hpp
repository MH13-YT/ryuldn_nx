#pragma once
#include <vapours.hpp>
#include "packet_id.hpp"

namespace ams::mitm::ldn::ryuldn {

    // RyuLDN Protocol Constants
    // Matches Ryujinx LdnRyu/Types/LdnHeader.cs
    constexpr u32 RyuLdnMagic = ('R' << 0) | ('L' << 8) | ('D' << 16) | ('N' << 24);  // "RLDN"
    constexpr u8 ProtocolVersion = 1;
    
    // Reduced from 128KB to 16KB - sufficient for network packets
    // Most LAN packets are <1500 bytes (MTU), control messages are <2KB
    // ProxyData with typical UDP packet: ~1500 bytes
    // Largest control message (ConnectRequest): 1276 bytes
    constexpr int MaxPacketSize = 16384;  // 16KB buffer

    // LDN Protocol Header (12 bytes - aligned layout to match C# without Pack=1)
    // C# StructLayout without Pack=1 adds 2-byte padding between Version and DataSize
    // Layout: Magic(4) | Type(1) | Version(1) | Padding(2) | DataSize(4)
    struct LdnHeader {
        u32 magic;      // Offset 0-3: 'RLDN' magic number
        u8 type;        // Offset 4: PacketId type
        u8 version;     // Offset 5: Protocol version
        u16 _padding;   // Offset 6-7: Padding for alignment (C# default behavior)
        s32 dataSize;   // Offset 8-11: Size of data following header
    };
    static_assert(sizeof(LdnHeader) == 0xC, "LdnHeader must be 0xC bytes (12) to match C# default alignment");

}
