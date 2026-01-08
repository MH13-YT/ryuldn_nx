#pragma once
#include <vapours.hpp>
#include "network_error.hpp"

namespace ams::mitm::ldn::ryuldn {

    // Network error notification message
    struct NetworkErrorMessage {
        NetworkError error;
        u8 padding[3];
    } __attribute__((packed));

}
