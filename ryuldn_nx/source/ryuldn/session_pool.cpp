#include "session_pool.hpp"
#include "proxy/p2p_proxy_session.hpp"
#include "proxy/p2p_proxy_server.hpp"
#include "../debug.hpp"

namespace ams::mitm::ldn::ryuldn::proxy {

    SessionPool::SessionPool(P2pProxyServer* server)
        : _freeList(nullptr),
          _mutex(false),
          _server(server),
          _activeCount(0)
    {
        // Initialize all slots as free with null session
        for (size_t i = 0; i < MaxPooledSessions; i++) {
            _sessions[i].session = nullptr;
            _sessions[i].inUse = false;
            _sessions[i].next = (i < MaxPooledSessions - 1) ? &_sessions[i + 1] : nullptr;
        }
        
        _freeList = &_sessions[0];
        
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "SessionPool: Initialized with capacity %zu", MaxPooledSessions);
    }

    SessionPool::~SessionPool() {
        Clear();
    }

    P2pProxySession* SessionPool::Acquire(s32 clientSocket) {
        std::scoped_lock lk(_mutex);

        // Find a free slot
        if (_freeList == nullptr) {
            LOG_INFO(COMP_RLDN_P2P_SRV, "SessionPool: All slots in use");
            return nullptr;
        }

        PooledSession* slot = _freeList;
        _freeList = slot->next;
        slot->next = nullptr;

        // Reuse existing session or create new one
        if (slot->session != nullptr) {
            // Reuse: Reset the session with new socket
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "SessionPool: Reusing session (ptr=%p)", slot->session);
            slot->session->Reset(_server, clientSocket);
        } else {
            // Create new session
            LOG_HEAP(COMP_RLDN_P2P_SRV, "before SessionPool new P2pProxySession");
            slot->session = new (std::nothrow) P2pProxySession(_server, clientSocket);
            
            if (slot->session == nullptr) {
                LOG_INFO(COMP_RLDN_P2P_SRV, "SessionPool: Failed to allocate session");
                LOG_HEAP(COMP_RLDN_P2P_SRV, "after SessionPool new P2pProxySession FAILED");
                
                // Return slot to free list
                slot->next = _freeList;
                _freeList = slot;
                return nullptr;
            }
            
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "SessionPool: Created new session (ptr=%p)", slot->session);
            LOG_HEAP(COMP_RLDN_P2P_SRV, "after SessionPool new P2pProxySession");
        }

        slot->inUse = true;
        _activeCount++;
        
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "SessionPool: Acquired session (active: %zu/%zu)", 
                 _activeCount, MaxPooledSessions);

        return slot->session;
    }

    void SessionPool::Release(P2pProxySession* session) {
        if (session == nullptr) {
            return;
        }

        std::scoped_lock lk(_mutex);

        // Find the slot for this session
        PooledSession* slot = nullptr;
        for (size_t i = 0; i < MaxPooledSessions; i++) {
            if (_sessions[i].session == session) {
                slot = &_sessions[i];
                break;
            }
        }

        if (slot == nullptr) {
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "SessionPool: WARNING - Unknown session (ptr=%p)", session);
            // This session is not from the pool, delete it
            delete session;
            return;
        }

        if (!slot->inUse) {
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "SessionPool: WARNING - Session already released (ptr=%p)", session);
            return;
        }

        // Session is kept alive for reuse
        // Just mark as not in use and return to free list
        slot->inUse = false;
        slot->next = _freeList;
        _freeList = slot;
        _activeCount--;

        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "SessionPool: Released session for reuse (active: %zu/%zu)", 
                 _activeCount, MaxPooledSessions);
    }

    void SessionPool::Clear() {
        std::scoped_lock lk(_mutex);

        // Delete all sessions
        for (size_t i = 0; i < MaxPooledSessions; i++) {
            if (_sessions[i].session != nullptr) {
                delete _sessions[i].session;
                _sessions[i].session = nullptr;
            }
            _sessions[i].inUse = false;
            _sessions[i].next = (i < MaxPooledSessions - 1) ? &_sessions[i + 1] : nullptr;
        }

        _freeList = &_sessions[0];
        _activeCount = 0;

        LOG_INFO(COMP_RLDN_P2P_SRV, "SessionPool: Cleared all sessions");
    }

} // namespace ams::mitm::ldn::ryuldn::proxy
