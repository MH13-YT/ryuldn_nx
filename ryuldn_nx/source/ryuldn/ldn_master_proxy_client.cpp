#include "ldn_master_proxy_client.hpp"
#include "../debug.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <cstring>

namespace ams::mitm::ldn::ryuldn {

    LdnMasterProxyClient::LdnMasterProxyClient(const char* serverAddress, int serverPort, bool useP2pProxy)
        : _serverAddress(serverAddress),
          _serverPort(serverPort),
          _socket(-1),
          _connected(false),
          _networkConnected(false),
          _stop(false),
          _useP2pProxy(useP2pProxy),
          _connectedEvent(nullptr),
          _errorEvent(nullptr),
          _scanEvent(nullptr),
          _rejectEvent(nullptr),
          _apConnectedEvent(nullptr),
          _disconnectReason(DisconnectReason::None),
          _lastError(NetworkError::None),
          _config{0, 0},
          _hostedProxy(nullptr),
          _connectedProxy(nullptr)
    {
        LogInfo("========================================");
        LogInfo("=== LdnMasterProxyClient Constructor ===");
        LogInfo("Server: %s:%d", serverAddress, serverPort);
        LogInfo("UseP2pProxy: %s", useP2pProxy ? "YES" : "NO");

        LogDebug("Initializing memory structures");
        std::memset(&_initializeMemory, 0, sizeof(_initializeMemory));
        std::memset(_gameVersion, 0, sizeof(_gameVersion));

        LogDebug("About to allocate thread stack (size=0x%zx + alignment=0x%zx, total=0x%zx)",
                 ThreadStackSize, os::ThreadStackAlignment, ThreadStackSize + os::ThreadStackAlignment);

        _threadStack = std::make_unique<u8[]>(ThreadStackSize + os::ThreadStackAlignment);

        if (!_threadStack) {
            LogError("CRITICAL: Failed to allocate thread stack!");
        } else {
            LogDebug("Thread stack allocated successfully at %p", _threadStack.get());
        }

        LogDebug("Allocating SystemEvents");
        _connectedEvent = new os::SystemEvent(os::EventClearMode_ManualClear, false);
        LogDebug("_connectedEvent created");
        _errorEvent = new os::SystemEvent(os::EventClearMode_ManualClear, false);
        LogDebug("_errorEvent created");
        _scanEvent = new os::SystemEvent(os::EventClearMode_ManualClear, false);
        LogDebug("_scanEvent created");
        _rejectEvent = new os::SystemEvent(os::EventClearMode_ManualClear, false);
        LogDebug("_rejectEvent created");
        _apConnectedEvent = new os::SystemEvent(os::EventClearMode_AutoClear, false);
        LogDebug("_apConnectedEvent created");

        LogDebug("Registering protocol callbacks (11 callbacks total)");
        // Setup protocol callbacks
        _protocol.onInitialize = [this](const LdnHeader& h, const InitializeMessage& m) { HandleInitialize(h, m); };
        _protocol.onConnected = [this](const LdnHeader& h, const NetworkInfo& i) { HandleConnected(h, i); };
        _protocol.onSyncNetwork = [this](const LdnHeader& h, const NetworkInfo& i) { HandleSyncNetwork(h, i); };
        _protocol.onDisconnected = [this](const LdnHeader& h, const DisconnectMessage& m) { HandleDisconnected(h, m); };
        _protocol.onRejectReply = [this](const LdnHeader& h) { HandleRejectReply(h); };
        _protocol.onScanReply = [this](const LdnHeader& h, const NetworkInfo& i) { HandleScanReply(h, i); };
        _protocol.onScanReplyEnd = [this](const LdnHeader& h) { HandleScanReplyEnd(h); };
        _protocol.onProxyConfig = [this](const LdnHeader& h, const ProxyConfig& c) { HandleProxyConfig(h, c); };
        _protocol.onProxyData = [this](const LdnHeader& h, const ProxyDataHeaderFull& hdr, const u8* p, u32 s) { HandleProxyData(h, hdr, p, s); };
        _protocol.onPing = [this](const LdnHeader& h, const PingMessage& p) { HandlePing(h, p); };
        _protocol.onNetworkError = [this](const LdnHeader& h, const NetworkErrorMessage& e) { HandleNetworkError(h, e); };
        _protocol.onExternalProxy = [this](const LdnHeader& h, const ExternalProxyConfig& c) { HandleExternalProxy(h, c); };

        LogInfo("LdnMasterProxyClient created successfully");
        LogInfo("========================================");
    }

    LdnMasterProxyClient::~LdnMasterProxyClient() {
        LogInfo("LdnMasterProxyClient destructor called");
        (void)Finalize();

        // Delete SystemEvents
        if (_connectedEvent) delete _connectedEvent;
        if (_errorEvent) delete _errorEvent;
        if (_scanEvent) delete _scanEvent;
        if (_rejectEvent) delete _rejectEvent;
        if (_apConnectedEvent) delete _apConnectedEvent;

        LogInfo("LdnMasterProxyClient destroyed");
    }

    Result LdnMasterProxyClient::Initialize() {
        LogFunctionEntry();
        LogInfo("Initializing LdnMasterProxyClient");

        if (_stop) {
            LogWarning("Initialize called but _stop flag is set!");
        }

        // Create worker thread
        uintptr_t stackAddr = reinterpret_cast<uintptr_t>(_threadStack.get());
        uintptr_t alignedAddr = util::AlignUp(stackAddr, os::ThreadStackAlignment);
        void* stackTop = reinterpret_cast<void*>(alignedAddr);

        LogDebug("Thread stack: raw=%p aligned=%p (offset=%zu)",
                 (void*)stackAddr, stackTop, alignedAddr - stackAddr);
        LogDebug("Creating worker thread: priority=0x15 (21), ideal_core=2");

        Result rc = os::CreateThread(&_workerThread, WorkerThreadFunc, this, stackTop, ThreadStackSize, 0x15, 2);
        if (R_FAILED(rc)) {
            LogError("CRITICAL: Failed to create worker thread: rc=0x%x", rc.GetValue());
            return rc;
        }

        LogThreadCreate("LdnMasterProxy::Worker", 0x15);
        os::StartThread(&_workerThread);
        LogThreadStart("LdnMasterProxy::Worker");

        LogInfo("LdnMasterProxyClient initialized successfully");
        LogFunctionExit();
        return ResultSuccess();
    }

    Result LdnMasterProxyClient::Finalize() {
        LogFunctionEntry();
        LogInfo("Finalizing LdnMasterProxyClient");

        if (_stop) {
            LogWarning("Finalize called but already stopped");
            return ResultSuccess();
        }

        LogInfo("Setting stop flag");
        _stop = true;

        if (_socket >= 0) {
            LogInfo("Disconnecting active connection (socket=%d)", _socket);
            Disconnect();
        } else {
            LogDebug("No active socket to disconnect");
        }

        LogInfo("Waiting for worker thread to terminate");
        os::WaitThread(&_workerThread);
        LogThreadExit("LdnMasterProxy::Worker");

        LogDebug("Destroying worker thread");
        os::DestroyThread(&_workerThread);

        LogInfo("LdnMasterProxyClient finalized successfully");
        LogFunctionExit();
        return ResultSuccess();
    }

    void LdnMasterProxyClient::WorkerThreadFunc(void* arg) {
        LogTrace("WorkerThreadFunc entry: arg=%p", arg);
        LdnMasterProxyClient* client = static_cast<LdnMasterProxyClient*>(arg);
        if (!client) {
            LogError("FATAL: WorkerThreadFunc received null client pointer!");
            return;
        }
        client->WorkerLoop();
        LogTrace("WorkerThreadFunc exit");
    }

    void LdnMasterProxyClient::WorkerLoop() {
        LogInfo("=== Worker thread started ===");
        LogDebug("Worker loop polling every 10ms");

        int iteration = 0;
        int errors_in_a_row = 0;

        while (!_stop) {
            if (_connected) {
                int rc = ReceiveData();
                if (rc < 0) {
                    errors_in_a_row++;
                    LogError("Receive error on iteration %d (errors_in_a_row=%d)", iteration, errors_in_a_row);
                    LogInfo("Disconnecting due to receive error");
                    Disconnect();
                    LogEventSignal("_errorEvent");
                    _errorEvent->Signal();
                } else if (rc > 0) {
                    // Reset error counter on successful receive
                    if (errors_in_a_row > 0) {
                        LogDebug("Recovered from errors (had %d errors)", errors_in_a_row);
                        errors_in_a_row = 0;
                    }
                }
                // rc == 0 means no data (EWOULDBLOCK), which is normal
            } else {
                // Log periodically when not connected
                if ((iteration % 1000) == 0) {  // Every ~10 seconds
                    LogTrace("Worker loop iteration %d: not connected", iteration);
                }
            }

            os::SleepThread(TimeSpan::FromMilliSeconds(10));
            iteration++;

            // Periodic health check log
            if ((iteration % 6000) == 0) {  // Every ~60 seconds
                LogDebug("Worker loop health check: iteration=%d connected=%d networkConnected=%d",
                         iteration, _connected ? 1 : 0, _networkConnected ? 1 : 0);
            }
        }

        LogInfo("=== Worker thread stopping (stop flag set) ===");
        LogInfo("Worker thread final stats: iterations=%d, connected=%d", iteration, _connected ? 1 : 0);
    }

    bool LdnMasterProxyClient::EnsureConnected() {
        if (_connected) {
            LogTrace("EnsureConnected: already connected");
            return true;
        }

        LogInfo("========================================");
        LogInfo("=== Establishing Connection to Master Server ===");
        LogInfo("Target: %s:%d", _serverAddress.c_str(), _serverPort);

        LogDebug("Clearing error and connected events");
        _errorEvent->Clear();
        _connectedEvent->Clear();

        // Create socket
        LogDebug("Creating TCP socket (AF_INET, SOCK_STREAM)");
        _socket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (_socket < 0) {
            LogError("CRITICAL: Failed to create socket: errno=%d (%s)", errno, strerror(errno));
            LogEventSignal("_errorEvent");
            _errorEvent->Signal();
            return false;
        }
        LogNetSocket(_socket, AF_INET, SOCK_STREAM, 0);
        LogInfo("Socket created successfully: fd=%d", _socket);

        // Set non-blocking
        LogDebug("Setting socket to non-blocking mode");
        int flags = fcntl(_socket, F_GETFL, 0);
        if (flags < 0) {
            LogWarning("fcntl F_GETFL failed: errno=%d", errno);
        }
        int rc = fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
        if (rc < 0) {
            LogError("fcntl F_SETFL failed: errno=%d", errno);
        } else {
            LogDebug("Socket set to non-blocking mode");
        }

        // Resolve hostname
        LogInfo("Resolving hostname: %s", _serverAddress.c_str());
        struct addrinfo hints, *result;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", _serverPort);

        int gai_rc = getaddrinfo(_serverAddress.c_str(), portStr, &hints, &result);
        if (gai_rc != 0) {
            LogError("CRITICAL: Failed to resolve hostname '%s': error=%d (%s)",
                     _serverAddress.c_str(), gai_rc, gai_strerror(gai_rc));
            LogNetClose(_socket);
            close(_socket);
            _socket = -1;
            LogEventSignal("_errorEvent");
            _errorEvent->Signal();
            return false;
        }

        if (result && result->ai_addr) {
            struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
            LogInfo("Hostname resolved to: %s:%d",
                    inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
        }

        // Connect
        LogInfo("Initiating TCP connection (non-blocking)");
        rc = ::connect(_socket, result->ai_addr, result->ai_addrlen);
        int connect_errno = errno;
        freeaddrinfo(result);

        if (rc < 0 && connect_errno != EINPROGRESS) {
            LogError("CRITICAL: connect() failed immediately: errno=%d (%s)",
                     connect_errno, strerror(connect_errno));
            LogNetClose(_socket);
            close(_socket);
            _socket = -1;
            LogEventSignal("_errorEvent");
            _errorEvent->Signal();
            return false;
        }

        if (rc == 0) {
            LogInfo("Connected immediately (rare for non-blocking socket)");
        } else {
            LogDebug("connect() returned EINPROGRESS, waiting for completion");
        }

        // Wait for connection with timeout
        LogInfo("Polling for connection completion (timeout=%dms)", FailureTimeout);
        struct pollfd pfd;
        pfd.fd = _socket;
        pfd.events = POLLOUT;
        pfd.revents = 0;

        rc = poll(&pfd, 1, FailureTimeout);
        if (rc < 0) {
            LogError("CRITICAL: poll() failed: errno=%d (%s)", errno, strerror(errno));
            LogNetClose(_socket);
            close(_socket);
            _socket = -1;
            LogEventSignal("_errorEvent");
            _errorEvent->Signal();
            return false;
        } else if (rc == 0) {
            LogError("CRITICAL: Connection timeout after %dms", FailureTimeout);
            LogNetClose(_socket);
            close(_socket);
            _socket = -1;
            LogEventSignal("_errorEvent");
            _errorEvent->Signal();
            return false;
        }

        LogDebug("poll() returned: revents=0x%x (POLLOUT=%d POLLERR=%d POLLHUP=%d)",
                 pfd.revents, (pfd.revents & POLLOUT) != 0,
                 (pfd.revents & POLLERR) != 0, (pfd.revents & POLLHUP) != 0);

        // Check if connected successfully
        LogDebug("Checking socket error status");
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(_socket, SOL_SOCKET, SO_ERROR, &error, &len);

        if (error != 0) {
            LogError("CRITICAL: Connection failed with socket error: %d (%s)", error, strerror(error));
            LogNetClose(_socket);
            close(_socket);
            _socket = -1;
            LogEventSignal("_errorEvent");
            _errorEvent->Signal();
            return false;
        }

        _connected = true;
        LogStateChange("disconnected", "connected");
        LogInfo("Protocol resetting");
        _protocol.Reset();
        LogEventSignal("_connectedEvent");
        _connectedEvent->Signal();

        LogInfo("✓ Connected to master server successfully");

        // Send initialize packet
        LogInfo("Sending Initialize packet");
        u8 packet[MaxPacketSize];
        int size = RyuLdnProtocol::Encode(PacketId::Initialize, _initializeMemory, packet);
        LogDebug("Initialize packet size: %d bytes", size);
        int sent = SendPacket(packet, size);
        if (sent < 0) {
            LogError("Failed to send Initialize packet");
        } else {
            LogInfo("Initialize packet sent successfully (%d bytes)", sent);
        }

        if (_passphrase.length() > 0) {
            LogDebug("Updating passphrase");
            UpdatePassphraseIfNeeded(_passphrase.c_str());
        }

        LogInfo("========================================");
        return true;
    }

    void LdnMasterProxyClient::Disconnect() {
        LogFunctionEntry();
        LogInfo("Disconnecting from master server");

        if (_socket >= 0) {
            LogInfo("Closing socket: fd=%d", _socket);
            LogNetClose(_socket);
            close(_socket);
            _socket = -1;
        } else {
            LogDebug("No socket to close (already -1)");
        }

        if (_connected) {
            _connected = false;
            LogStateChange("connected", "disconnected");
        }

        if (_networkConnected) {
            LogInfo("Network was connected, calling DisconnectInternal");
            DisconnectInternal();
        } else {
            LogDebug("Network was not connected");
        }

        LogInfo("Disconnect complete");
    }

    void LdnMasterProxyClient::DisconnectInternal() {
        LogFunctionEntry();
        LogInfo("Internal network disconnect");

        if (_networkConnected) {
            _networkConnected = false;
            LogStateChange("network_connected", "network_disconnected");

            // Clean up P2P proxy resources
            if (_hostedProxy) {
                LogInfo("Disposing hosted proxy at %p", (void*)_hostedProxy);
                _hostedProxy->Dispose();
                LogMemFree(_hostedProxy);
                delete _hostedProxy;
                _hostedProxy = nullptr;
            } else {
                LogDebug("No hosted proxy to dispose");
            }

            if (_connectedProxy) {
                LogInfo("Disconnecting connected proxy at %p", (void*)_connectedProxy);
                _connectedProxy->Disconnect();
                LogMemFree(_connectedProxy);
                delete _connectedProxy;
                _connectedProxy = nullptr;
            } else {
                LogDebug("No connected proxy to disconnect");
            }

            LogDebug("Clearing AP connected event");
            os::ClearSystemEvent(_apConnectedEvent->GetBase());

            if (_networkChangeCallback) {
                LogInfo("Notifying network change callback (disconnected)");
                NetworkInfo emptyInfo;
                std::memset(&emptyInfo, 0, sizeof(emptyInfo));
                _networkChangeCallback(emptyInfo, false);
            } else {
                LogDebug("No network change callback registered");
            }
        } else {
            LogDebug("DisconnectInternal called but network was not connected");
        }

        LogInfo("Internal disconnect complete");
    }

    int LdnMasterProxyClient::SendPacket(const u8* data, int size) {
        LogTrace("SendPacket: size=%d", size);

        if (!_connected || _socket < 0) {
            LogError("SendPacket: not connected (connected=%d socket=%d)", _connected ? 1 : 0, _socket);
            return -1;
        }

        if (!data || size <= 0) {
            LogError("SendPacket: invalid parameters (data=%p size=%d)", (void*)data, size);
            return -1;
        }

        if (size > MaxPacketSize) {
            LogError("SendPacket: size %d exceeds MaxPacketSize %d", size, MaxPacketSize);
            return -1;
        }

        std::lock_guard<std::mutex> lock(_sendMutex);

        int sent = 0;
        int retry_count = 0;
        constexpr int MaxRetries = 1000;  // ~1 second at 1ms per retry

        while (sent < size) {
            int rc = ::send(_socket, data + sent, size - sent, 0);
            if (rc < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    retry_count++;
                    if (retry_count > MaxRetries) {
                        LogError("SendPacket: exceeded max retries (%d), aborting", MaxRetries);
                        return -1;
                    }
                    if ((retry_count % 100) == 0) {
                        LogWarning("SendPacket: socket blocking, retry %d/%d", retry_count, MaxRetries);
                    }
                    os::SleepThread(TimeSpan::FromMilliSeconds(1));
                    continue;
                }
                LogError("SendPacket: send() error at offset %d/%d: errno=%d (%s)",
                         sent, size, errno, strerror(errno));
                LogNetError(_socket, errno, "send failed");
                return -1;
            }
            sent += rc;
            LogTrace("SendPacket: sent %d bytes (total %d/%d)", rc, sent, size);
        }

        if (retry_count > 0) {
            LogDebug("SendPacket: completed after %d retries", retry_count);
        }
        LogNetSend(_socket, sent);
        return sent;
    }

    int LdnMasterProxyClient::SendRawPacket(const u8* data, int size) {
        LogTrace("SendRawPacket: forwarding to SendPacket, size=%d", size);
        return SendPacket(data, size);
    }

    int LdnMasterProxyClient::ReceiveData() {
        LogTrace("ReceiveData entry");

        if (!_connected || _socket < 0) {
            LogError("ReceiveData: not connected (connected=%d socket=%d)", _connected ? 1 : 0, _socket);
            return -1;
        }

        u8 buffer[4096];
        int received = ::recv(_socket, buffer, sizeof(buffer), MSG_DONTWAIT);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available - this is normal
                return 0;
            }
            LogError("ReceiveData: recv() error: errno=%d (%s)", errno, strerror(errno));
            LogNetError(_socket, errno, "recv failed");
            return -1;
        }

        if (received == 0) {
            LogWarning("ReceiveData: Connection closed by server (recv returned 0)");
            return -1;
        }

        LogNetRecv(_socket, received);
        LogDebug("ReceiveData: received %d bytes, passing to protocol parser", received);

        // Pass to protocol parser
        _protocol.Read(buffer, 0, received);

        return received;
    }

    void LdnMasterProxyClient::UpdatePassphraseIfNeeded(const char* passphrase) {
        if (!passphrase || strlen(passphrase) == 0) {
            return;
        }

        if (_passphrase == passphrase) {
            return;
        }

        _passphrase = passphrase;

        if (_connected) {
            u8 packet[MaxPacketSize];
            PassphraseMessage msg;
            std::memset(&msg, 0, sizeof(msg));
            strncpy(msg.passphrase, passphrase, sizeof(msg.passphrase) - 1);

            int size = RyuLdnProtocol::Encode(PacketId::Passphrase, msg, packet);
            SendPacket(packet, size);

            LogFormat("Sent passphrase update");
        }
    }

    void LdnMasterProxyClient::SetNetworkChangeCallback(NetworkChangeCallback callback) {
        _networkChangeCallback = callback;
    }

    void LdnMasterProxyClient::SetProxyConfigCallback(ProxyConfigCallback callback) {
        _proxyConfigCallback = callback;
    }

    void LdnMasterProxyClient::SetProxyDataCallback(ProxyDataCallback callback) {
        _proxyDataCallback = callback;
    }

    void LdnMasterProxyClient::SetGameVersion(const u8* version, size_t size) {
        size_t copySize = std::min(size, sizeof(_gameVersion));
        std::memcpy(_gameVersion, version, copySize);
    }

    void LdnMasterProxyClient::SetPassphrase(const char* passphrase) {
        UpdatePassphraseIfNeeded(passphrase);
    }

    // Protocol event handlers
    void LdnMasterProxyClient::HandleInitialize([[maybe_unused]] const LdnHeader& header, const InitializeMessage& msg) {
        LogFunctionEntry();
        LogInfo("Received Initialize response from server");
        _initializeMemory = msg;
        LogDebug("Initialize memory updated");
    }

    void LdnMasterProxyClient::HandleConnected([[maybe_unused]] const LdnHeader& header, const NetworkInfo& info) {
        LogFunctionEntry();
        LogInfo("========================================");
        LogInfo("=== NETWORK CONNECTED ===");
        LogInfo("SSID: %.*s", (int)info.common.ssid.length, info.common.ssid.raw);
        LogInfo("Channel: %d", info.common.channel);
        LogInfo("Players: %d/%d", info.ldn.nodeCount, info.ldn.nodeCountMax);

        _networkConnected = true;
        LogStateChange("network_disconnected", "network_connected");

        _disconnectReason = DisconnectReason::None;

        LogEventSignal("_apConnectedEvent");
        _apConnectedEvent->Signal();

        if (_networkChangeCallback) {
            LogDebug("Calling network change callback (connected=true)");
            _networkChangeCallback(info, true);
        } else {
            LogWarning("No network change callback registered!");
        }

        LogInfo("========================================");
    }

    void LdnMasterProxyClient::HandleSyncNetwork([[maybe_unused]] const LdnHeader& header, const NetworkInfo& info) {
        LogFunctionEntry();
        LogInfo("Network sync - players: %d/%d", info.ldn.nodeCount, info.ldn.nodeCountMax);

        if (_networkChangeCallback) {
            LogDebug("Calling network change callback for sync");
            _networkChangeCallback(info, true);
        } else {
            LogWarning("No network change callback for sync!");
        }
    }

    void LdnMasterProxyClient::HandleDisconnected([[maybe_unused]] const LdnHeader& header, [[maybe_unused]] const DisconnectMessage& msg) {
        LogFunctionEntry();
        LogWarning("========================================");
        LogWarning("=== NETWORK DISCONNECTED ===");
        LogWarning("Reason code: %d", (int)msg.reason);
        LogWarning("========================================");
        DisconnectInternal();
    }

    void LdnMasterProxyClient::HandleRejectReply([[maybe_unused]] const LdnHeader& header) {
        LogFunctionEntry();
        LogWarning("Connection/action was REJECTED by server");
        LogEventSignal("_rejectEvent");
        _rejectEvent->Signal();
    }

    void LdnMasterProxyClient::HandleScanReply([[maybe_unused]] const LdnHeader& header, const NetworkInfo& info) {
        LogTrace("HandleScanReply: adding network to available games");
        LogDebug("Scan result: SSID='%.*s' players=%d/%d",
                 (int)info.common.ssid.length, info.common.ssid.raw,
                 info.ldn.nodeCount, info.ldn.nodeCountMax);
        _availableGames.push_back(info);
    }

    void LdnMasterProxyClient::HandleScanReplyEnd([[maybe_unused]] const LdnHeader& header) {
        LogFunctionEntry();
        LogInfo("Scan complete: found %zu networks", _availableGames.size());
        LogEventSignal("_scanEvent");
        _scanEvent->Signal();
    }

    void LdnMasterProxyClient::HandleProxyConfig([[maybe_unused]] const LdnHeader& header, const ProxyConfig& config) {
        LogFunctionEntry();
        LogInfo("Received ProxyConfig: proxyIp=0x%08x proxySubnetMask=0x%08x",
                config.proxyIp, config.proxySubnetMask);
        _config = config;
        LogFormat("Received proxy config: IP=0x%08x, Mask=0x%08x", config.proxyIp, config.proxySubnetMask);

        // Notify callback
        if (_proxyConfigCallback) {
            _proxyConfigCallback(header, config);
        }
    }

    void LdnMasterProxyClient::HandleProxyData([[maybe_unused]] const LdnHeader& header, const ProxyDataHeaderFull& hdr, const u8* payload, u32 payloadSize) {
        // Forward to proxy via callback
        if (_proxyDataCallback) {
            _proxyDataCallback(header, hdr, payload, payloadSize);
        }
    }

    void LdnMasterProxyClient::HandlePing([[maybe_unused]] const LdnHeader& header, const PingMessage& ping) {
        if (ping.requester == 0) {
            // Server requested ping, send it back
            u8 packet[MaxPacketSize];
            int size = RyuLdnProtocol::Encode(PacketId::Ping, ping, packet);
            SendPacket(packet, size);
        }
    }

    void LdnMasterProxyClient::HandleNetworkError([[maybe_unused]] const LdnHeader& header, const NetworkErrorMessage& error) {
        if (error.error == NetworkError::PortUnreachable) {
            // Disable P2P proxy if we get port unreachable error
            _useP2pProxy = false;
            LogFormat("Network error: PortUnreachable - P2P proxy disabled");
        } else {
            _lastError = error.error;
            LogFormat("Network error: %d", static_cast<int>(error.error));
        }
    }

    void LdnMasterProxyClient::HandleExternalProxy([[maybe_unused]] const LdnHeader& header, const ExternalProxyConfig& config) {
        LogFormat("Received external proxy config - creating P2P proxy client");

        // Convert proxy IP bytes to string
        char ipStr[INET_ADDRSTRLEN];
        u32 ipv4 = 0;

        // Extract IPv4 address from config (last 4 bytes of IPv6-mapped address)
        if (config.addressFamily == AF_INET) {
            std::memcpy(&ipv4, config.proxyIp + 12, 4);
            struct in_addr addr;
            addr.s_addr = ipv4;
            inet_ntop(AF_INET, &addr, ipStr, sizeof(ipStr));
        } else {
            LogFormat("HandleExternalProxy: Unsupported address family %d", config.addressFamily);
            DisconnectInternal();
            return;
        }

        // Create P2P proxy client
        proxy::P2pProxyClient* client = new proxy::P2pProxyClient(ipStr, config.proxyPort);
        _connectedProxy = client;

        // Connect and authenticate
        if (!client->Connect()) {
            LogFormat("HandleExternalProxy: Failed to connect to proxy");
            delete client;
            _connectedProxy = nullptr;
            DisconnectInternal();
            return;
        }

        if (!client->PerformAuth(config)) {
            LogFormat("HandleExternalProxy: Failed to authenticate");
            delete client;
            _connectedProxy = nullptr;
            DisconnectInternal();
            return;
        }

        LogFormat("HandleExternalProxy: Successfully connected to P2P proxy at %s:%u", ipStr, config.proxyPort);
    }

    NetworkError LdnMasterProxyClient::ConsumeNetworkError() {
        NetworkError result = _lastError;
        _lastError = NetworkError::None;
        return result;
    }

    // LDN Operations
    Result LdnMasterProxyClient::CreateNetwork(const CreateAccessPointRequest& request, const u8* advertiseData, u16 advertiseDataSize) {
        LogFormat("CreateNetwork");

        if (!EnsureConnected()) {
            return MAKERESULT(0xFD, 1);
        }

        UpdatePassphraseIfNeeded(_passphrase.c_str());

        // Clean up any existing hosted proxy
        if (_hostedProxy) {
            _hostedProxy->Dispose();
            delete _hostedProxy;
            _hostedProxy = nullptr;
        }

        // Try to create P2P proxy server if enabled
        u16 internalProxyPort = 0;
        u16 externalProxyPort = 0;
        bool openSuccess = false;

        if (_useP2pProxy) {
            // Try to create proxy server on available port
            for (s32 i = 0; i < proxy::P2pProxyServer::PrivatePortRange; i++) {
                u16 port = proxy::P2pProxyServer::PrivatePortBase + i;
                _hostedProxy = new proxy::P2pProxyServer(this, port, &_protocol);

                if (_hostedProxy->Start()) {
                    // Successfully started
                    openSuccess = true;
                    internalProxyPort = port;

                    // Try UPnP port mapping
                    externalProxyPort = _hostedProxy->NatPunch();

                    if (externalProxyPort == 0) {
                        // UPnP failed, but we can still use internal port
                        LogFormat("CreateNetwork: UPnP NAT punch failed, using internal port only");
                        externalProxyPort = internalProxyPort;
                    } else {
                        LogFormat("CreateNetwork: UPnP NAT punch success %u -> %u", internalProxyPort, externalProxyPort);
                    }

                    break;
                } else {
                    // Port unavailable, try next
                    delete _hostedProxy;
                    _hostedProxy = nullptr;
                }
            }

            if (!openSuccess) {
                LogFormat("CreateNetwork: Failed to open P2P proxy server");
                _useP2pProxy = false;
            }
        }

        // Copy game version into request
        CreateAccessPointRequest modifiedRequest = request;
        std::memcpy(modifiedRequest.ryuNetworkConfig.gameVersion, _gameVersion, sizeof(_gameVersion));

        // Set proxy ports if available
        if (openSuccess && _useP2pProxy) {
            modifiedRequest.ryuNetworkConfig.internalProxyPort = internalProxyPort;
            modifiedRequest.ryuNetworkConfig.externalProxyPort = externalProxyPort;
        }

        u8 packet[MaxPacketSize];
        int size = RyuLdnProtocol::Encode(PacketId::CreateAccessPoint, modifiedRequest, advertiseData, advertiseDataSize, packet);
        SendPacket(packet, size);

        // Send immediate dummy network change to avoid game crashes
        if (_networkChangeCallback) {
            NetworkInfo dummyInfo;
            std::memset(&dummyInfo, 0, sizeof(dummyInfo));
            std::memcpy(dummyInfo.common.bssid.raw, _initializeMemory.macAddress, 6);
            dummyInfo.common.channel = request.networkConfig.channel;
            dummyInfo.common.linkLevel = 3;
            dummyInfo.common.networkType = 2;
            dummyInfo.ldn.advertiseDataSize = advertiseDataSize;
            dummyInfo.ldn.nodeCount = 1;
            dummyInfo.ldn.nodeCountMax = request.networkConfig.nodeCountMax;
            dummyInfo.ldn.securityMode = request.securityConfig.securityMode;
            dummyInfo.ldn.nodes[0].ipv4Address = 0x0a730b01;  // Dummy IP
            std::memcpy(dummyInfo.ldn.nodes[0].macAddress.raw, _initializeMemory.macAddress, 6);
            dummyInfo.ldn.nodes[0].isConnected = 1;
            dummyInfo.ldn.nodes[0].nodeId = 0;
            dummyInfo.ldn.nodes[0].localCommunicationVersion = request.networkConfig.localCommunicationVersion;

            _networkChangeCallback(dummyInfo, true);
        }

        // Wait for actual connection
        bool signalled = _apConnectedEvent->TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout));

        if (signalled) {
            return ResultSuccess();
        } else {
            return MAKERESULT(0xFD, 2);
        }
    }

    Result LdnMasterProxyClient::CreateNetworkPrivate(const CreateAccessPointPrivateRequest& request, const u8* advertiseData, u16 advertiseDataSize) {
        LogFormat("CreateNetworkPrivate");

        if (!EnsureConnected()) {
            return MAKERESULT(0xFD, 1);
        }

        UpdatePassphraseIfNeeded(_passphrase.c_str());

        CreateAccessPointPrivateRequest modifiedRequest = request;
        std::memcpy(modifiedRequest.ryuNetworkConfig.gameVersion, _gameVersion, sizeof(_gameVersion));

        u8 packet[MaxPacketSize];
        int size = RyuLdnProtocol::Encode(PacketId::CreateAccessPointPrivate, modifiedRequest, advertiseData, advertiseDataSize, packet);
        SendPacket(packet, size);

        bool signalled = _apConnectedEvent->TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout));

        if (signalled) {
            return ResultSuccess();
        } else {
            return MAKERESULT(0xFD, 2);
        }
    }

    Result LdnMasterProxyClient::Connect(const ConnectRequest& request) {
        LogFormat("Connect");

        if (!EnsureConnected()) {
            return MAKERESULT(0xFD, 1);
        }

        u8 packet[MaxPacketSize];
        int size = RyuLdnProtocol::Encode(PacketId::Connect, request, packet);
        SendPacket(packet, size);

        // Send immediate dummy network change
        if (_networkChangeCallback) {
            _networkChangeCallback(request.networkInfo, true);
        }

        bool signalled = _apConnectedEvent->TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout));

        // Wait for proxy to be ready if we're using P2P proxy client
        if (signalled && _connectedProxy) {
            if (!_connectedProxy->EnsureProxyReady()) {
                LogFormat("Connect: P2P proxy not ready");
                return MAKERESULT(0xFD, 3);
            }
            _config = _connectedProxy->GetProxyConfig();
        }

        NetworkError error = ConsumeNetworkError();
        if (error != NetworkError::None) {
            return MAKERESULT(0xFD, static_cast<int>(error));
        }

        if (signalled) {
            return ResultSuccess();
        } else {
            return MAKERESULT(0xFD, static_cast<int>(NetworkError::ConnectTimeout));
        }
    }

    Result LdnMasterProxyClient::ConnectPrivate(const ConnectPrivateRequest& request) {
        LogFormat("ConnectPrivate");

        if (!EnsureConnected()) {
            return MAKERESULT(0xFD, 1);
        }

        u8 packet[MaxPacketSize];
        int size = RyuLdnProtocol::Encode(PacketId::ConnectPrivate, request, packet);
        SendPacket(packet, size);

        bool signalled = _apConnectedEvent->TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout));

        // Wait for proxy to be ready if we're using P2P proxy client
        if (signalled && _connectedProxy) {
            if (!_connectedProxy->EnsureProxyReady()) {
                LogFormat("ConnectPrivate: P2P proxy not ready");
                return MAKERESULT(0xFD, 3);
            }
            _config = _connectedProxy->GetProxyConfig();
        }

        NetworkError error = ConsumeNetworkError();
        if (error != NetworkError::None) {
            return MAKERESULT(0xFD, static_cast<int>(error));
        }

        if (signalled) {
            return ResultSuccess();
        } else {
            return MAKERESULT(0xFD, static_cast<int>(NetworkError::ConnectTimeout));
        }
    }

    Result LdnMasterProxyClient::DisconnectNetwork() {
        LogFormat("DisconnectNetwork");

        if (_networkConnected) {
            DisconnectMessage msg;
            msg.reason = DisconnectReason::DisconnectedByUser;

            u8 packet[MaxPacketSize];
            int size = RyuLdnProtocol::Encode(PacketId::Disconnect, msg, packet);
            SendPacket(packet, size);

            DisconnectInternal();
        }

        return ResultSuccess();
    }

    Result LdnMasterProxyClient::Scan(NetworkInfo* networks, u16* count, const ScanFilter& filter) {
        LogFormat("Scan");

        _availableGames.clear();

        if (!EnsureConnected()) {
            *count = 0;
            return MAKERESULT(0xFD, 1);
        }

        UpdatePassphraseIfNeeded(_passphrase.c_str());

        _scanEvent->Clear();

        u8 packet[MaxPacketSize];
        int size = RyuLdnProtocol::Encode(PacketId::Scan, filter, packet);
        SendPacket(packet, size);

        bool signalled = _scanEvent->TimedWait(TimeSpan::FromMilliSeconds(ScanTimeout));

        if (!signalled) {
            *count = 0;
            return ResultSuccess();
        }

        u16 found = std::min(static_cast<u16>(_availableGames.size()), *count);
        for (u16 i = 0; i < found; i++) {
            networks[i] = _availableGames[i];
        }
        *count = found;

        LogFormat("Scan found %d networks", found);

        return ResultSuccess();
    }

    Result LdnMasterProxyClient::SetAdvertiseData(const u8* data, u16 size) {
        LogFormat("SetAdvertiseData");

        if (!_networkConnected) {
            return MAKERESULT(0xFD, 3);
        }

        u8 packet[MaxPacketSize];
        int packetSize = RyuLdnProtocol::Encode(PacketId::SetAdvertiseData, data, size, packet);
        SendPacket(packet, packetSize);

        return ResultSuccess();
    }

    Result LdnMasterProxyClient::SetStationAcceptPolicy(u8 acceptPolicy) {
        LogFormat("SetStationAcceptPolicy: %d", acceptPolicy);

        if (!_networkConnected) {
            return MAKERESULT(0xFD, 3);
        }

        SetAcceptPolicyRequest request;
        request.stationAcceptPolicy = acceptPolicy;

        u8 packet[MaxPacketSize];
        int size = RyuLdnProtocol::Encode(PacketId::SetAcceptPolicy, request, packet);
        SendPacket(packet, size);

        return ResultSuccess();
    }

    Result LdnMasterProxyClient::Reject(DisconnectReason reason, u32 nodeId) {
        LogFormat("Reject node %d", nodeId);

        if (!_networkConnected) {
            return MAKERESULT(0xFD, 3);
        }

        _rejectEvent->Clear();

        RejectRequest request;
        request.disconnectReason = reason;
        request.nodeId = nodeId;

        u8 packet[MaxPacketSize];
        int size = RyuLdnProtocol::Encode(PacketId::Reject, request, packet);
        SendPacket(packet, size);

        bool signalled = _rejectEvent->TimedWait(TimeSpan::FromMilliSeconds(InactiveTimeout));

        if (!signalled) {
            return MAKERESULT(0xFD, 4);
        }

        NetworkError error = ConsumeNetworkError();
        if (error != NetworkError::None) {
            return MAKERESULT(0xFD, static_cast<int>(error));
        }

        return ResultSuccess();
    }
}
