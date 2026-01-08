#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Set station accept policy request
    // Matches Ryujinx LdnRyu/Types/SetAcceptPolicyRequest.cs
    struct SetAcceptPolicyRequest {
        u8 stationAcceptPolicy;     // Accept policy mode
        u8 padding[3];
    } __attribute__((packed));

}
