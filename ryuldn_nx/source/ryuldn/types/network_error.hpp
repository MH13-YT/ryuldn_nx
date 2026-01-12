#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Network error codes (matches LanPlay/Ryujinx server)
    enum class NetworkError : int32_t {
        None = 0,

        PortUnreachable = 1,

        TooManyPlayers = 2,
        VersionTooLow = 3,
        VersionTooHigh = 4,

        ConnectFailure = 5,
        ConnectNotFound = 6,
        ConnectTimeout = 7,
        ConnectRejected = 8,

        RejectFailed = 9,

        BannedByServer = 127,

        Unknown = -1
    };

}
