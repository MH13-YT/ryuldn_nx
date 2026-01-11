#pragma once
#include <vapours.hpp>
#include "packet_id.hpp"

namespace ams::mitm::ldn::ryuldn {

    // RyuLDN Protocol Constants
    // Matches Ryujinx LdnRyu/Types/LdnHeader.cs
    constexpr u32 RyuLdnMagic = ('R' << 0) | ('L' << 8) | ('D' << 16) | ('N' << 24);  // "RLDN"
    constexpr u8 ProtocolVersion = 1;
    constexpr int MaxPacketSize = 16384;  // 16KB max packet size (reasonable for LDN packets)

    // LDN Protocol Header
    // Matches Ryujinx LdnRyu/Types/LdnHeader.cs
    // NOTE: C# adds padding between version and dataSize for alignment
    struct LdnHeader {
        u32 magic;          // Offset 0-3: 'RLDN' magic number
        u8 type;            // Offset 4: PacketId type
        u8 version;         // Offset 5: Protocol version
        u16 _padding;       // Offset 6-7: Padding added by C# StructLayout
        s32 dataSize;       // Offset 8-11: Size of data following header
    } __attribute__((packed));
    static_assert(sizeof(LdnHeader) == 0xC, "LdnHeader must be 12 bytes with C# padding");

}
