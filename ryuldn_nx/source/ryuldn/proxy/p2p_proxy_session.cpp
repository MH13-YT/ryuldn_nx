#include "p2p_proxy_session.hpp"
#include "p2p_proxy_server.hpp"
#include "../../debug.hpp"
#include <unistd.h>
#include <cstring>

namespace ams::mitm::ldn::ryuldn::proxy {

    P2pProxySession::P2pProxySession(P2pProxyServer* server, s32 clientSocket)
        : _parent(server),
          _socket(clientSocket),
          _virtualIpAddress(0),
          _masterClosed(false),
          _running(false),
          _protocol(g_sharedBufferPool),  // Use shared BufferPool
          _receiveThread{},  // Zero-initialize thread structure
          _sendMutex(false)
    {
        LOG_HEAP(COMP_RLDN_P2P_SES, "P2pProxySession constructor start");
        
        // Allocate small receive buffer for socket recv() only (not for packet assembly)
        // Protocol will borrow from BufferPool when needed
        constexpr size_t SmallBufferSize = 8192;  // Enough for socket recv chunks
        _receiveBuffer.reset(new (std::nothrow) u8[SmallBufferSize]);
        
        if (!_receiveBuffer) {
            AMS_ABORT("P2pProxySession: Failed to allocate %zu byte receive buffer", SmallBufferSize);
        }
        
        // Register protocol handlers
        _protocol.onExternalProxy = [this](const LdnHeader& header, const ExternalProxyConfig& token) {
            HandleAuthentication(header, token);
        };

        _protocol.onProxyDisconnect = [this](const LdnHeader& header, const ProxyDisconnectMessageFull& message) {
            HandleProxyDisconnect(header, message);
        };

        _protocol.onProxyData = [this](const LdnHeader& header, const ProxyDataHeaderFull& hdr, const u8* data, u32 dataSize) {
            HandleProxyData(header, hdr, data, dataSize);
        };

        _protocol.onProxyConnectReply = [this](const LdnHeader& header, const ProxyConnectResponseFull& response) {
            HandleProxyConnectReply(header, response);
        };

        _protocol.onProxyConnect = [this](const LdnHeader& header, const ProxyConnectRequestFull& request) {
            HandleProxyConnect(header, request);
        };

        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "P2pProxySession: Created for socket %d", _socket);
        LOG_HEAP(COMP_RLDN_P2P_SES, "P2pProxySession constructor end");
    }

    P2pProxySession::~P2pProxySession() {
        Stop();

        if (_socket >= 0) {
            close(_socket);
            _socket = -1;
        }

        LOG_INFO(COMP_RLDN_P2P_SES, "P2pProxySession: Destroyed");
    }

    bool P2pProxySession::Start() {
        if (_running) {
            return false;
        }

        // Allocate thread stack with explicit nothrow allocation
        LOG_HEAP(COMP_RLDN_P2P_SES, "before P2pProxySession thread stack");
        _threadStack.reset(new (std::nothrow) u8[ThreadStackSize + os::ThreadStackAlignment]);
        
        if (!_threadStack) {
            LOG_INFO(COMP_RLDN_P2P_SES, "P2pProxySession: Failed to allocate thread stack");
            LOG_HEAP(COMP_RLDN_P2P_SES, "after P2pProxySession thread stack FAILED");
            return false;
        }
        
        LOG_HEAP(COMP_RLDN_P2P_SES, "after P2pProxySession thread stack");
        void* stackTop = reinterpret_cast<void*>(util::AlignUp(reinterpret_cast<uintptr_t>(_threadStack.get()), os::ThreadStackAlignment));

        // Create receive thread - reinitialize to ensure clean state
        std::memset(&_receiveThread, 0, sizeof(_receiveThread));
        LOG_INFO(COMP_RLDN_P2P_SES, "[THREAD-DIAG] === Creating P2P-SESSION RECEIVE THREAD ===");
        LOG_INFO(COMP_RLDN_P2P_SES, "[THREAD-DIAG]   Thread type: RECEIVE (P2P session)");
        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "[THREAD-DIAG]   Thread struct @ %p", &_receiveThread);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "[THREAD-DIAG]   Stack buffer @ %p", _threadStack.get());
        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "[THREAD-DIAG]   Stack top (aligned) @ %p", stackTop);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "[THREAD-DIAG]   Stack size: 0x%x (%d bytes)", ThreadStackSize, ThreadStackSize);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "[THREAD-DIAG]   Priority: %d (0x%x)", 21, 21);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "[THREAD-DIAG]   Ideal core: %d", 3);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "[THREAD-DIAG]   Alignment check: 0x%lx & 0xF = 0x%lx", (uintptr_t)stackTop, (uintptr_t)stackTop & 0xF);
        Result rc = os::CreateThread(&_receiveThread, ReceiveThreadFunc, this, stackTop, ThreadStackSize, 21, 3);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "[THREAD-DIAG] >>> CreateThread returned: 0x%x (%s)", rc.GetValue(), R_SUCCEEDED(rc) ? "SUCCESS" : "FAILED");
        if (R_FAILED(rc)) {
            LOG_INFO(COMP_RLDN_P2P_SES, "[THREAD-DIAG] !!! RECEIVE THREAD CREATION FAILED (SESSION) !!!");
            LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "[THREAD-DIAG]   Error code: 0x%x", rc.GetValue());
            return false;
        }

        _running = true;
        os::StartThread(&_receiveThread);

        LOG_INFO(COMP_RLDN_P2P_SES, "P2pProxySession: Started");
        return true;
    }

    void P2pProxySession::Stop() {
        if (!_running) {
            return;
        }

        _running = false;

        // Shutdown socket to wake up receive thread
        if (_socket >= 0) {
            shutdown(_socket, SHUT_RDWR);
        }

        os::WaitThread(&_receiveThread);
        os::DestroyThread(&_receiveThread);
        std::memset(&_receiveThread, 0, sizeof(_receiveThread));

        LOG_INFO(COMP_RLDN_P2P_SES, "P2pProxySession: Stopped");
    }

    void P2pProxySession::DisconnectAndStop() {
        _masterClosed = true;
        Stop();
    }

    void P2pProxySession::ReceiveThreadFunc(void* arg) {
        P2pProxySession* session = static_cast<P2pProxySession*>(arg);
        session->ReceiveLoop();
    }

    void P2pProxySession::ReceiveLoop() {
        u8* buffer = _receiveBuffer.get();
        constexpr size_t bufferSize = 8192;

        while (_running) {
            ssize_t received = recv(_socket, buffer, bufferSize, 0);

            if (received > 0) {
                _protocol.Read(buffer, 0, received);
            } else if (received == 0) {
                LOG_INFO(COMP_RLDN_P2P_SES, "P2pProxySession: Client disconnected");
                break;
            } else {
                if (errno != EINTR) {
                    LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "P2pProxySession: Receive error: %d", errno);
                    break;
                }
            }
        }

        // Notify parent of disconnection (if not master-initiated)
        if (!_masterClosed && _parent) {
            _parent->DisconnectProxyClient(this);
        }
    }

    void P2pProxySession::Reset(P2pProxyServer* server, s32 clientSocket) {
        // Reset all state for session reuse
        Stop();  // Ensure any previous connection is properly closed
        
        _parent = server;
        _socket = clientSocket;
        _virtualIpAddress = 0;
        _masterClosed = false;
        _running = false;
        
        // Reset protocol state
        _protocol.Reset();
        
        LOG_INFO_ARGS(COMP_RLDN_P2P_SES, "P2pProxySession: Reset for socket %d", _socket);
    }

    bool P2pProxySession::SendAsync(const u8* data, size_t size) {
        if (_socket < 0) {
            return false;
        }

        // Thread-safe send operation (NetCoreServer behavior)
        std::lock_guard<os::Mutex> lock(_sendMutex);

        ssize_t sent = send(_socket, data, size, 0);
        return sent == static_cast<ssize_t>(size);
    }

    void P2pProxySession::HandleAuthentication([[maybe_unused]] const LdnHeader& header, const ExternalProxyConfig& token) {
        if (!_parent->TryRegisterUser(this, token)) {
            LOG_INFO(COMP_RLDN_P2P_SES, "P2pProxySession: Authentication failed");
            DisconnectAndStop();
        } else {
            LOG_INFO(COMP_RLDN_P2P_SES, "P2pProxySession: Authenticated successfully");
        }
    }

    void P2pProxySession::HandleProxyDisconnect(const LdnHeader& header, const ProxyDisconnectMessageFull& message) {
        _parent->HandleProxyDisconnect(this, header, message);
    }

    void P2pProxySession::HandleProxyData(const LdnHeader& header, const ProxyDataHeaderFull& message, const u8* data, u32 dataSize) {
        _parent->HandleProxyData(this, header, message, data, dataSize);
    }

    void P2pProxySession::HandleProxyConnectReply(const LdnHeader& header, const ProxyConnectResponseFull& data) {
        _parent->HandleProxyConnectReply(this, header, data);
    }

    void P2pProxySession::HandleProxyConnect(const LdnHeader& header, const ProxyConnectRequestFull& message) {
        _parent->HandleProxyConnect(this, header, message);
    }

} // namespace ams::mitm::ldn::ryuldn::proxy
