#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // RyuLDN Protocol Packet Types
    // Matches Ryujinx LdnRyu/Types/PacketId.cs
    enum class PacketId : u8 {
        Initialize = 0,
        Passphrase = 1,

        CreateAccessPoint = 2,
        CreateAccessPointPrivate = 3,
        ExternalProxy = 4,
        ExternalProxyToken = 5,
        ExternalProxyState = 6,
        SyncNetwork = 7,
        Reject = 8,
        RejectReply = 9,
        Scan = 10,
        ScanReply = 11,
        ScanReplyEnd = 12,
        Connect = 13,
        ConnectPrivate = 14,
        Connected = 15,
        Disconnect = 16,

        ProxyConfig = 17,
        ProxyConnect = 24,
        ProxyConnectReply = 26,
        ProxyData = 27,
        ProxyDisconnect = 28,

        SetAcceptPolicy = 29,
        SetAdvertiseData = 30,

        Ping = 254,
        NetworkError = 255
    };

}
