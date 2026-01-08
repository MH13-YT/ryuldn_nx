#pragma once
#include <stratosphere.hpp>
#include <arpa/inet.h>
#include <cstring>

namespace ams::mitm::ldn::ryuldn::proxy {

    // Utility functions for proxy operations
    // Matches Ryujinx LdnRyu/Proxy/ProxyHelpers.cs

    // Convert sockaddr_in to u32 IP address
    inline u32 SockAddrToU32(const sockaddr_in* addr) {
        return ntohl(addr->sin_addr.s_addr);
    }

    // Convert u32 IP address and port to sockaddr_in
    inline void U32ToSockAddr(u32 ip, u16 port, sockaddr_in* addr) {
        std::memset(addr, 0, sizeof(sockaddr_in));
        addr->sin_family = AF_INET;
        addr->sin_addr.s_addr = htonl(ip);
        addr->sin_port = htons(port);
    }

    // Check if IP is in subnet
    inline bool IsInSubnet(u32 ip, u32 subnet_base, u32 subnet_mask) {
        return (ip & subnet_mask) == (subnet_base & subnet_mask);
    }

    // Calculate broadcast address
    inline u32 CalculateBroadcastAddress(u32 ip, u32 subnet_mask) {
        return ip | (~subnet_mask);
    }

}
