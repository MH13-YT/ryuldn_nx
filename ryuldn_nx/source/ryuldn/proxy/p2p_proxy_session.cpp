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
          _running(false)
    {
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

        LogFormat("P2pProxySession: Created for socket %d", _socket);
    }

    P2pProxySession::~P2pProxySession() {
        Stop();

        if (_socket >= 0) {
            close(_socket);
            _socket = -1;
        }

        LogFormat("P2pProxySession: Destroyed");
    }

    bool P2pProxySession::Start() {
        if (_running) {
            return false;
        }

        // Allocate thread stack
        _threadStack = std::make_unique<u8[]>(ThreadStackSize + os::ThreadStackAlignment);
        void* stackTop = reinterpret_cast<void*>(util::AlignUp(reinterpret_cast<uintptr_t>(_threadStack.get()), os::ThreadStackAlignment));

        // Create receive thread
        Result rc = os::CreateThread(&_receiveThread, ReceiveThreadFunc, this, stackTop, ThreadStackSize, 0x2C, 3);
        if (R_FAILED(rc)) {
            LogFormat("P2pProxySession: Failed to create receive thread: 0x%x", rc);
            return false;
        }

        _running = true;
        os::StartThread(&_receiveThread);

        LogFormat("P2pProxySession: Started");
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

        LogFormat("P2pProxySession: Stopped");
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
        u8 buffer[8192];

        while (_running) {
            ssize_t received = recv(_socket, buffer, sizeof(buffer), 0);

            if (received > 0) {
                _protocol.Read(buffer, 0, received);
            } else if (received == 0) {
                LogFormat("P2pProxySession: Client disconnected");
                break;
            } else {
                if (errno != EINTR) {
                    LogFormat("P2pProxySession: Receive error: %d", errno);
                    break;
                }
            }
        }

        // Notify parent of disconnection (if not master-initiated)
        if (!_masterClosed && _parent) {
            _parent->DisconnectProxyClient(this);
        }
    }

    bool P2pProxySession::SendAsync(const u8* data, size_t size) {
        if (_socket < 0) {
            return false;
        }

        ssize_t sent = send(_socket, data, size, 0);
        return sent == static_cast<ssize_t>(size);
    }

    void P2pProxySession::HandleAuthentication([[maybe_unused]] const LdnHeader& header, const ExternalProxyConfig& token) {
        if (!_parent->TryRegisterUser(this, token)) {
            LogFormat("P2pProxySession: Authentication failed");
            DisconnectAndStop();
        } else {
            LogFormat("P2pProxySession: Authenticated successfully");
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
