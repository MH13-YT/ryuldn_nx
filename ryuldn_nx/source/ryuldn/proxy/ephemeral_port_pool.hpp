#pragma once
// Ephemeral Port Pool
// Matches Ryujinx LdnRyu/Proxy/EphemeralPortPool.cs
// Manages allocation and tracking of ephemeral ports

#include <vapours.hpp>
#include <stratosphere.hpp>
#include <vector>
#include <algorithm>

namespace ams::mitm::ldn::ryuldn::proxy {

    class EphemeralPortPool {
    private:
        static constexpr u16 EphemeralBase = 49152;
        static constexpr u16 EphemeralEnd = 65535;

        std::vector<u16> _ephemeralPorts; // Sorted list of allocated ports
        os::Mutex _lock;

    public:
        EphemeralPortPool() : _lock(false) {
            _ephemeralPorts.reserve(1024); // Reserve space for typical usage
        }

        ~EphemeralPortPool() = default;

        // Get an available ephemeral port
        // Returns 0 if the range is exhausted
        u16 AllocatePort() {
            std::scoped_lock lk(_lock);

            u16 port = EphemeralBase;

            // Starting at the ephemeral port base, return an ephemeral port that is not in use
            for (size_t i = 0; i < _ephemeralPorts.size(); i++) {
                u16 existingPort = _ephemeralPorts[i];

                if (existingPort > port) {
                    // The port was free - take it
                    _ephemeralPorts.insert(_ephemeralPorts.begin() + i, port);
                    return port;
                }

                port++;

                // Check if we've exhausted the range
                if (port == 0 || port > EphemeralEnd) {
                    return 0; // No ports available
                }
            }

            // If we get here, add to the end of the list
            if (port != 0 && port <= EphemeralEnd) {
                _ephemeralPorts.push_back(port);
                return port;
            }

            return 0; // No ports available
        }

        // Return a port to the pool
        void ReturnPort(u16 port) {
            std::scoped_lock lk(_lock);

            // Remove the port from the allocated list
            auto it = std::find(_ephemeralPorts.begin(), _ephemeralPorts.end(), port);
            if (it != _ephemeralPorts.end()) {
                _ephemeralPorts.erase(it);
            }
        }

        // Get number of allocated ports (for debugging)
        size_t GetAllocatedCount() const {
            // Note: not thread-safe, for debugging only
            return _ephemeralPorts.size();
        }
    };

} // namespace ams::mitm::ldn::ryuldn::proxy
