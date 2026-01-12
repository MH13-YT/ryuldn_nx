#pragma once
#include <vapours.hpp>
#include "network_error.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Network error notification message (4 bytes)
    struct NetworkErrorMessage {
        NetworkError error;
    } __attribute__((packed));
    static_assert(sizeof(NetworkErrorMessage) == 0x4, "NetworkErrorMessage must be 0x4 bytes");

}
