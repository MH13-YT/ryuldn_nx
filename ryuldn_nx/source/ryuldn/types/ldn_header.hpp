#pragma once
#include <vapours.hpp>
#include "packet_id.hpp"

namespace ams::mitm::ldn::ryuldn {

    // RyuLDN Protocol Constants
    // Matches Ryujinx LdnRyu/Types/LdnHeader.cs
    constexpr u32 RyuLdnMagic = ('R' << 0) | ('L' << 8) | ('D' << 16) | ('N' << 24);  // "RLDN"
    constexpr u8 ProtocolVersion = 1;
    constexpr int MaxPacketSize = 131072;  // 128KB max packet size

    // LDN Protocol Header (10 bytes)
    // Matches Ryujinx LdnRyu/Types/LdnHeader.cs
    struct LdnHeader {
        u32 magic;          // 'RLDN' magic number
        u8 type;            // PacketId type
        u8 version;         // Protocol version
        s32 dataSize;       // Size of data following header
    } __attribute__((packed));
    static_assert(sizeof(LdnHeader) == 0xA, "LdnHeader must be 10 bytes");

}
