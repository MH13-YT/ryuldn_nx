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

    // LDN Protocol Header (10 bytes - matches C# StructLayout(LayoutKind.Sequential, Size = 0xA))
    // Must match official ldn-master server exactly
    // Layout: Magic(4) | Type(1) | Version(1) | DataSize(4)
    struct LdnHeader {
        u32 magic;      // Offset 0-3: 'RLDN' magic number
        u8 type;        // Offset 4: PacketId type
        u8 version;     // Offset 5: Protocol version
        s32 dataSize;   // Offset 6-9: Size of data following header
    } __attribute__((packed));
    static_assert(sizeof(LdnHeader) == 0xA, "LdnHeader must be 0xA bytes (10) to match C# server");

}
