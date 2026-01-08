#pragma once

namespace ams::mitm::ldn::ryuldn {

    // Network timeout constants (milliseconds)
    // Matches Ryujinx LdnRyu/NetworkTimeout.cs
    constexpr int InactiveTimeout = 6000;  // Disconnects after 6s of inactivity
    constexpr int FailureTimeout = 4000;   // Connection failure timeout
    constexpr int ScanTimeout = 1000;      // Scan operation timeout

}
