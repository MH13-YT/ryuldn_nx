#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Network error codes
    // Matches Ryujinx network error handling
    enum class NetworkError : u8 {
        None = 0,
        PortUnreachable = 1,
        TooManyPlayers = 2,
        VersionTooLow = 3,
        VersionTooHigh = 4,
        ConnectNotFound = 5,
        ConnectTimeout = 6,
        ConnectRejected = 7,
        ResetByPeer = 8,
        Unknown = 255
    };

}
