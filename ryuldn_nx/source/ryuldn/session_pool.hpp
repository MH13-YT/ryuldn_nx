#pragma once
// P2pProxySession Pool
// Reuse session objects to reduce allocation overhead
// Particularly useful when clients connect/disconnect frequently

#include <stratosphere.hpp>
#include "types.hpp"
#include <list>

namespace ams::mitm::ldn::ryuldn::proxy {

    // Forward declaration
    class P2pProxySession;
    class P2pProxyServer;

    /**
     * Session Pool for P2pProxySession reuse
     * 
     * BENEFITS:
     * - Reduces allocation overhead (session + thread stack)
     * - Better memory locality
     * - Faster session creation
     * 
     * USAGE:
     * - Acquire() a session when client connects
     * - Release() when client disconnects (resets state)
     * - Session is recycled for next connection
     */
    class SessionPool {
    private:
        struct PooledSession {
            P2pProxySession* session;
            bool inUse;
            PooledSession* next;
        };

        static constexpr size_t MaxPooledSessions = 4;  // Max concurrent clients

        PooledSession _sessions[MaxPooledSessions];
        PooledSession* _freeList;
        os::Mutex _mutex;
        P2pProxyServer* _server;
        size_t _activeCount;

    public:
        explicit SessionPool(P2pProxyServer* server);
        ~SessionPool();

        /**
         * Acquire a session for a new client connection
         * Returns existing unused session or creates new one
         * Returns nullptr if pool is exhausted and allocation fails
         */
        P2pProxySession* Acquire(s32 clientSocket);

        /**
         * Release a session back to the pool
         * Resets session state for reuse
         */
        void Release(P2pProxySession* session);

        /**
         * Get number of active sessions
         */
        size_t GetActiveCount() const { return _activeCount; }

        /**
         * Clear all sessions (called on server shutdown)
         */
        void Clear();
    };

} // namespace ams::mitm::ldn::ryuldn::proxy
