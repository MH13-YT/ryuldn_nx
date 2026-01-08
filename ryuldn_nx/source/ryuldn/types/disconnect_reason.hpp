#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Disconnect reason codes
    // Matches Nintendo LDN disconnect reasons
    enum class DisconnectReason : u32 {
        None = 0,
        DisconnectedByUser = 1,
        DisconnectedBySystem = 2,
        DestroyedByUser = 3,
        DestroyedBySystem = 4,
        Rejected = 5,
        SignalLost = 6
    };

}
