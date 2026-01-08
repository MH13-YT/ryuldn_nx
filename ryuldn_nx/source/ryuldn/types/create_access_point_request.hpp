#pragma once
#include <vapours.hpp>
#include "../../ldn_types.hpp"
#include "ryu_network_config.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Create access point request (188 bytes)
    // Matches Ryujinx CreateAccessPointRequest
    struct CreateAccessPointRequest {
        SecurityConfig securityConfig;
        UserConfig userConfig;
        NetworkConfig networkConfig;
        RyuNetworkConfig ryuNetworkConfig;
    } __attribute__((packed));
    static_assert(sizeof(CreateAccessPointRequest) == 0xBC, "CreateAccessPointRequest must be 0xBC bytes");

    // Create private access point request
    struct CreateAccessPointPrivateRequest {
        SecurityConfig securityConfig;
        SecurityParameter securityParameter;
        UserConfig userConfig;
        NetworkConfig networkConfig;
        RyuNetworkConfig ryuNetworkConfig;
    } __attribute__((packed));

}
