#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Set station accept policy request (1 byte)
    // Matches Ryujinx LdnRyu/Types/SetAcceptPolicyRequest.cs
    struct SetAcceptPolicyRequest {
        u8 stationAcceptPolicy;     // Accept policy mode
    } __attribute__((packed));
    static_assert(sizeof(SetAcceptPolicyRequest) == 0x1, "SetAcceptPolicyRequest must be 0x1 byte");

}
