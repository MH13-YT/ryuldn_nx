#pragma once
#include <vapours.hpp>
#include "../../ldn_types.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Connect to network request (1276 bytes)
    // Matches Ryujinx ConnectRequest
    #pragma pack(push, 1)
    struct ConnectRequest {
        SecurityConfig securityConfig;
        UserConfig userConfig;
        u32 localCommunicationVersion;
        u32 optionUnknown;
        NetworkInfo networkInfo;
    };
    #pragma pack(pop)
    static_assert(sizeof(ConnectRequest) == 0x4FC, "ConnectRequest must be 0x4FC bytes");

    // Connect to private network request
    struct ConnectPrivateRequest {
        SecurityConfig securityConfig;
        SecurityParameter securityParameter;
        UserConfig userConfig;
        u32 localCommunicationVersion;
        u32 optionUnknown;
        NetworkConfig networkConfig;
    } __attribute__((packed));
    static_assert(sizeof(ConnectPrivateRequest) == 0xBC, "ConnectPrivateRequest must be 0xBC bytes");

}
