#include "p2p_proxy_client.hpp"
#include "ldn_proxy.hpp"
#include "../../debug.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <netinet/tcp.h>

namespace ams::mitm::ldn::ryuldn::proxy {

    P2pProxyClient::P2pProxyClient(const std::string& address, u16 port)
        : _address(address),
          _port(port),
          _socket(-1),
          _connected(false),
          _ready(false),
          _running(false),
          _protocol(g_sharedBufferPool),  // Use shared BufferPool
          _receiveThread{},  // Zero-initialize thread structure
          _connectedEvent(os::EventClearMode_ManualClear, true),
          _readyEvent(os::EventClearMode_ManualClear, true),
          _stateMutex(false),
          _sendMutex(false)
    {
        LOG_HEAP(COMP_RLDN_P2P_CLI, "P2pProxyClient constructor start");
        
        // Register protocol handler
        _protocol.onProxyConfig = [this](const LdnHeader& header, const ProxyConfig& config) {
            HandleProxyConfig(header, config);
        };

        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "P2pProxyClient: Created for %s:%u", _address.c_str(), _port);
        LOG_HEAP(COMP_RLDN_P2P_CLI, "P2pProxyClient constructor end");
    }

    P2pProxyClient::~P2pProxyClient() {
        Disconnect();
    }

    bool P2pProxyClient::Connect() {
        if (_connected) {
            return true;
        }

        // Create socket
        _socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_socket < 0) {
            LOG_INFO(COMP_RLDN_P2P_CLI, "P2pProxyClient: Failed to create socket");
            return false;
        }

        // Set TCP_NODELAY if supported
        int optval = 1;
        setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

        // Connect to server
        sockaddr_in serverAddr;
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(_port);

        if (inet_pton(AF_INET, _address.c_str(), &serverAddr.sin_addr) <= 0) {
            LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "P2pProxyClient: Invalid address %s", _address.c_str());
            close(_socket);
            _socket = -1;
            return false;
        }

        if (connect(_socket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
            LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "P2pProxyClient: Failed to connect to %s:%u (errno=%d)", _address.c_str(), _port, errno);
            close(_socket);
            _socket = -1;
            return false;
        }

        // Allocate thread stack with explicit nothrow allocation
        _threadStack.reset(new (std::nothrow) u8[ThreadStackSize + os::ThreadStackAlignment]);
        
        if (!_threadStack) {
            LOG_INFO(COMP_RLDN_P2P_CLI, "P2pProxyClient: Failed to allocate thread stack");
            close(_socket);
            _socket = -1;
            return false;
        }
        
        void* stackTop = reinterpret_cast<void*>(util::AlignUp(reinterpret_cast<uintptr_t>(_threadStack.get()), os::ThreadStackAlignment));

        // Create receive thread - reinitialize to ensure clean state
        std::memset(&_receiveThread, 0, sizeof(_receiveThread));
        LOG_INFO(COMP_RLDN_P2P_CLI, "[THREAD-DIAG] === Creating P2P-CLIENT RECEIVE THREAD ===");
        LOG_INFO(COMP_RLDN_P2P_CLI, "[THREAD-DIAG]   Thread type: RECEIVE (P2P client)");
        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "[THREAD-DIAG]   Thread struct @ %p", &_receiveThread);
        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "[THREAD-DIAG]   Stack buffer @ %p", _threadStack.get());
        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "[THREAD-DIAG]   Stack top (aligned) @ %p", stackTop);
        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "[THREAD-DIAG]   Stack size: 0x%x (%d bytes)", ThreadStackSize, ThreadStackSize);
        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "[THREAD-DIAG]   Priority: %d (0x%x)", 21, 21);
        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "[THREAD-DIAG]   Ideal core: %d", 3);
        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "[THREAD-DIAG]   Alignment check: 0x%lx & 0xF = 0x%lx", (uintptr_t)stackTop, (uintptr_t)stackTop & 0xF);
        Result rc = os::CreateThread(&_receiveThread, ReceiveThreadFunc, this, stackTop, ThreadStackSize, 21, 3);
        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "[THREAD-DIAG] >>> CreateThread returned: 0x%x (%s)", rc.GetValue(), R_SUCCEEDED(rc) ? "SUCCESS" : "FAILED");
        if (R_FAILED(rc)) {
            LOG_INFO(COMP_RLDN_P2P_CLI, "[THREAD-DIAG] !!! RECEIVE THREAD CREATION FAILED (CLIENT) !!!");
            LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "[THREAD-DIAG]   Error code: 0x%x", rc.GetValue());
            close(_socket);
            _socket = -1;
            return false;
        }

        _connected = true;
        _running = true;
        os::SignalSystemEvent(_connectedEvent.GetBase());
        os::StartThread(&_receiveThread);

        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "P2pProxyClient: Connected to %s:%u", _address.c_str(), _port);
        return true;
    }

    void P2pProxyClient::Disconnect() {
        if (!_connected && !_running) {
            return;
        }

        _running = false;
        _connected = false;
        _ready = false;

        os::ClearSystemEvent(_connectedEvent.GetBase());
        os::ClearSystemEvent(_readyEvent.GetBase());

        // Shutdown socket to wake up receive thread
        if (_socket >= 0) {
            shutdown(_socket, SHUT_RDWR);
        }

        // Wait for receive thread
        if (_running) {
            os::WaitThread(&_receiveThread);
            os::DestroyThread(&_receiveThread);
            std::memset(&_receiveThread, 0, sizeof(_receiveThread));
        }

        // Close socket
        if (_socket >= 0) {
            close(_socket);
            _socket = -1;
        }

        LOG_INFO(COMP_RLDN_P2P_CLI, "P2pProxyClient: Disconnected");
    }

    void P2pProxyClient::ReceiveThreadFunc(void* arg) {
        P2pProxyClient* client = static_cast<P2pProxyClient*>(arg);
        client->ReceiveLoop();
    }

    void P2pProxyClient::ReceiveLoop() {
        u8 buffer[ReceiveBufferSize];

        while (_running) {
            ssize_t received = recv(_socket, buffer, sizeof(buffer), 0);

            if (received > 0) {
                _protocol.Read(buffer, 0, received);
            } else if (received == 0) {
                LOG_INFO(COMP_RLDN_P2P_CLI, "P2pProxyClient: Server disconnected");
                break;
            } else {
                if (errno != EINTR) {
                    LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "P2pProxyClient: Receive error: %d", errno);
                    break;
                }
            }
        }

        // Mark as disconnected
        {
            std::scoped_lock lk(_stateMutex);
            _connected = false;
            _ready = false;
            os::ClearSystemEvent(_connectedEvent.GetBase());
            os::ClearSystemEvent(_readyEvent.GetBase());
        }
    }

    void P2pProxyClient::HandleProxyConfig([[maybe_unused]] const LdnHeader& header, const ProxyConfig& config) {
        std::scoped_lock lk(_stateMutex);
        _proxyConfig = config;
        _ready = true;
        os::SignalSystemEvent(_readyEvent.GetBase());

        LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "P2pProxyClient: Received proxy config - IP=0x%08x, Mask=0x%08x",
                 config.proxyIp, config.proxySubnetMask);

        // TODO: Register proxy with socket helpers (requires integration with BSD socket layer)
        // In Ryujinx this calls SocketHelpers.RegisterProxy(new LdnProxy(config, this, _protocol))
    }

    bool P2pProxyClient::PerformAuth(const ExternalProxyConfig& config) {
        // Wait for connection
        TimeSpan timeout = TimeSpan::FromMilliSeconds(FailureTimeoutMs);
        os::ClearSystemEvent(_connectedEvent.GetBase());
        if (!os::TimedWaitSystemEvent(_connectedEvent.GetBase(), timeout)) {
            LOG_INFO(COMP_RLDN_P2P_CLI, "P2pProxyClient: Connection timeout");
            return false;
        }

        if (!_connected) {
            LOG_INFO(COMP_RLDN_P2P_CLI, "P2pProxyClient: Not connected");
            return false;
        }

        // Send authentication
        ScopedBuffer buffer(g_sharedBufferPool);
        if (!buffer.Get()) {
            LOG_ERR(COMP_RLDN_P2P_CLI, "P2pProxyClient: Failed to borrow buffer for auth");
            return false;
        }
        
        int packetSize = RyuLdnProtocol::Encode(PacketId::ExternalProxy, config, buffer.Get());

        if (!SendAsync(buffer.Get(), packetSize)) {
            LOG_INFO(COMP_RLDN_P2P_CLI, "P2pProxyClient: Failed to send authentication");
            return false;
        }

        LOG_INFO(COMP_RLDN_P2P_CLI, "P2pProxyClient: Authentication sent");
        return true;
    }

    bool P2pProxyClient::EnsureProxyReady() {
        TimeSpan timeout = TimeSpan::FromMilliSeconds(FailureTimeoutMs);
        os::ClearSystemEvent(_readyEvent.GetBase());
        if (!os::TimedWaitSystemEvent(_readyEvent.GetBase(), timeout)) {
            LOG_INFO(COMP_RLDN_P2P_CLI, "P2pProxyClient: Proxy ready timeout");
            return false;
        }

        return _ready;
    }

    bool P2pProxyClient::SendAsync(const u8* data, size_t size) {
        if (_socket < 0 || !_connected) {
            return false;
        }

        // Thread-safe send operation (NetCoreServer behavior)
        std::lock_guard<os::Mutex> lock(_sendMutex);

        ssize_t sent = send(_socket, data, size, 0);
        if (sent != static_cast<ssize_t>(size)) {
            LOG_INFO_ARGS(COMP_RLDN_P2P_CLI, "P2pProxyClient: Send failed (sent=%ld, expected=%zu)", sent, size);
            return false;
        }

        return true;
    }

} // namespace ams::mitm::ldn::ryuldn::proxy
