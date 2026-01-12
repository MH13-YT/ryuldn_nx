#pragma once
// SystemEvent Optimization
// Replace dynamic allocations with direct members
// Saves ~240 bytes + allocation overhead per component

#include <stratosphere.hpp>

namespace ams::mitm::ldn::ryuldn {

    /**
     * SystemEvent Container
     * Holds SystemEvents as direct members instead of heap allocations
     * Reduces fragmentation and eliminates allocation failures
     */
    struct SystemEventContainer {
        os::SystemEvent connectedEvent;
        os::SystemEvent errorEvent;
        os::SystemEvent scanEvent;
        os::SystemEvent rejectEvent;
        os::SystemEvent apConnectedEvent;
        os::SystemEvent initializeEvent;

        SystemEventContainer()
            : connectedEvent(os::EventClearMode_ManualClear, false),
              errorEvent(os::EventClearMode_ManualClear, false),
              scanEvent(os::EventClearMode_ManualClear, false),
              rejectEvent(os::EventClearMode_ManualClear, false),
              apConnectedEvent(os::EventClearMode_AutoClear, false),
              initializeEvent(os::EventClearMode_AutoClear, false)
        {}

        // No copy/move
        SystemEventContainer(const SystemEventContainer&) = delete;
        SystemEventContainer& operator=(const SystemEventContainer&) = delete;
    };

} // namespace ams::mitm::ldn::ryuldn
