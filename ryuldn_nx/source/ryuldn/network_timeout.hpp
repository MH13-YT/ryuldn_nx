#pragma once

#include <stratosphere.hpp>
#include <functional>
#include <atomic>
#include <memory>

namespace ams::mitm::ldn::ryuldn {

    // Network timeout constants (milliseconds)
    // Matches Ryujinx LdnRyu/NetworkTimeout.cs
    constexpr int InactiveTimeout = 6000;  // Disconnects after 6s of inactivity
    constexpr int FailureTimeout = 4000;   // Connection failure timeout
    constexpr int ScanTimeout = 1000;      // Scan operation timeout

    // Network timeout handler
    // Matches Ryujinx LdnRyu/NetworkTimeout.cs behavior
    // NOTE: Uses simple timestamp-based checking instead of threads to avoid C++ exception allocation issues on Switch
    class NetworkTimeout {
    private:
        int _idleTimeout;
        std::function<void()> _timeoutCallback;
        std::atomic<u64> _lastRefreshTime;
        std::atomic<bool> _active;
        os::Mutex _lock;

    public:
        NetworkTimeout(int idleTimeout, std::function<void()> callback)
            : _idleTimeout(idleTimeout), 
              _timeoutCallback(callback),
              _lastRefreshTime(0),
              _active(true),
              _lock(false) {}

        ~NetworkTimeout() {
            DisableTimeout();
        }

        bool RefreshTimeout() {
            std::scoped_lock lock(_lock);
            
            if (_active) {
                // Just update the last refresh time - checking is done elsewhere
                _lastRefreshTime = os::ConvertToTimeSpan(os::GetSystemTick()).GetMilliSeconds();
            }
            return true;
        }

        // Call this periodically from WorkerLoop to check for timeouts
        void CheckTimeout() {
            std::scoped_lock lock(_lock);
            
            if (!_active || !_timeoutCallback) {
                return;
            }

            u64 now = os::ConvertToTimeSpan(os::GetSystemTick()).GetMilliSeconds();
            u64 elapsed = now - _lastRefreshTime;
            
            if (elapsed >= static_cast<u64>(_idleTimeout)) {
                _timeoutCallback();
                _lastRefreshTime = now;  // Reset for next timeout
            }
        }

        void DisableTimeout() {
            std::scoped_lock lock(_lock);
            _active = false;
        }

        void Dispose() {
            DisableTimeout();
        }
    };

}
