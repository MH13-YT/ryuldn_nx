#include "p2p_proxy_server.hpp"
#include "../ldn_master_proxy_client.hpp"
#include "../../debug.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <algorithm>

namespace ams::mitm::ldn::ryuldn::proxy {

    P2pProxyServer::P2pProxyServer(LdnMasterProxyClient* master, u16 port, RyuLdnProtocol* masterProtocol)
        : _privatePort(port),
          _publicPort(0),
          _listenSocket(-1),
          _running(false),
          _disposed(false),
          _broadcastAddress(0),
          _hasPortMapping(false),
          _master(master),
          _masterProtocol(masterProtocol),
          _protocol(g_sharedBufferPool),  // Use shared BufferPool
          _tokensLock(false),
          _tokenEvent(os::EventClearMode_ManualClear, true),
          _leaseThread{},  // Zero-initialize thread structures
          _leaseThreadRunning(false),
          _acceptThread{},  // Zero-initialize thread structures
          _sessionPool(this),  // Initialize session pool
          _playersLock(false)
    {
        LOG_HEAP(COMP_RLDN_P2P_SRV, "P2pProxyServer constructor start");
        
        // Register handlers with master protocol
        _masterProtocol->onExternalProxyToken = [this](const LdnHeader& header, const ExternalProxyToken& token) {
            HandleToken(header, token);
        };

        _masterProtocol->onExternalProxyState = [this](const LdnHeader& header, const ExternalProxyConnectionState& state) {
            HandleStateChange(header, state);
        };

        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Created on port %u", _privatePort);
        LOG_HEAP(COMP_RLDN_P2P_SRV, "P2pProxyServer constructor end");
    }

    P2pProxyServer::~P2pProxyServer() {
        Dispose();
    }

    bool P2pProxyServer::Start() {
        if (_running) {
            return false;
        }

        // Create listen socket
        _listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_listenSocket < 0) {
            LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Failed to create listen socket");
            return false;
        }

        // Set socket options
        int optval = 1;
        setsockopt(_listenSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        // Bind to port
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(_privatePort);

        if (bind(_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Failed to bind to port %u", _privatePort);
            close(_listenSocket);
            _listenSocket = -1;
            return false;
        }

        // Listen
        if (listen(_listenSocket, 10) < 0) {
            LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Failed to listen");
            close(_listenSocket);
            _listenSocket = -1;
            return false;
        }

        // Allocate accept thread stack with explicit nothrow allocation
        LOG_HEAP(COMP_RLDN_P2P_SRV, "before P2pProxyServer accept thread stack");
        _acceptThreadStack.reset(new (std::nothrow) u8[AcceptThreadStackSize + os::ThreadStackAlignment]);
        
        if (!_acceptThreadStack) {
            LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Failed to allocate accept thread stack");
            LOG_HEAP(COMP_RLDN_P2P_SRV, "after P2pProxyServer accept thread stack FAILED");
            close(_listenSocket);
            _listenSocket = -1;
            return false;
        }
        LOG_HEAP(COMP_RLDN_P2P_SRV, "after P2pProxyServer accept thread stack");
        void* acceptStackTop = reinterpret_cast<void*>(util::AlignUp(reinterpret_cast<uintptr_t>(_acceptThreadStack.get()), os::ThreadStackAlignment));

        // Create accept thread - reinitialize to ensure clean state
        std::memset(&_acceptThread, 0, sizeof(_acceptThread));
        LOG_INFO(COMP_RLDN_P2P_SRV, "[THREAD-DIAG] === Creating P2P-SERVER ACCEPT THREAD ===");
        LOG_INFO(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Thread type: ACCEPT (P2P relay server)");
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Thread struct @ %p", &_acceptThread);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Stack buffer @ %p", _acceptThreadStack.get());
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Stack top (aligned) @ %p", acceptStackTop);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Stack size: 0x%x (%d bytes)", AcceptThreadStackSize, AcceptThreadStackSize);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Priority: %d (0x%x)", 21, 21);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Ideal core: %d", 3);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Stack alignment: %lu bytes", os::ThreadStackAlignment);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Alignment check: 0x%lx & 0xF = 0x%lx", (uintptr_t)acceptStackTop, (uintptr_t)acceptStackTop & 0xF);
        Result rc = os::CreateThread(&_acceptThread, AcceptThreadFunc, this, acceptStackTop, AcceptThreadStackSize, 21, 3);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG] >>> CreateThread returned: 0x%x (%s)", rc.GetValue(), R_SUCCEEDED(rc) ? "SUCCESS" : "FAILED");
        if (R_FAILED(rc)) {
            LOG_INFO(COMP_RLDN_P2P_SRV, "[THREAD-DIAG] !!! ACCEPT THREAD CREATION FAILED !!!");
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Error code: 0x%x", rc.GetValue());
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "[THREAD-DIAG]   Stack allocated: %s", _acceptThreadStack ? "YES" : "NO");
            close(_listenSocket);
            _listenSocket = -1;
            std::memset(&_acceptThread, 0, sizeof(_acceptThread));  // Clean up on failure
            return false;
        }

        _running = true;
        os::StartThread(&_acceptThread);

        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Started on port %u", _privatePort);
        return true;
    }

    void P2pProxyServer::Stop() {
        if (!_running) {
            return;
        }

        LOG_INFO(COMP_RLDN_P2P_SRV, "[THREAD-DIAG] Stopping P2P Server threads...");
        _running = false;

        // Close listen socket to wake up accept thread
        if (_listenSocket >= 0) {
            shutdown(_listenSocket, SHUT_RDWR);
            close(_listenSocket);
            _listenSocket = -1;
        }

        // Wait for accept thread
        os::WaitThread(&_acceptThread);
        os::DestroyThread(&_acceptThread);
        std::memset(&_acceptThread, 0, sizeof(_acceptThread));

        // Stop all sessions via pool
        _sessionPool.Clear();
        _players.clear();

        // Stop lease renewal thread
        if (_leaseThreadRunning) {
            _leaseThreadRunning = false;
            os::WaitThread(&_leaseThread);
            os::DestroyThread(&_leaseThread);
            std::memset(&_leaseThread, 0, sizeof(_leaseThread));
        }

        LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Stopped");
    }

    void P2pProxyServer::Dispose() {
        if (_disposed) {
            return;
        }

        _disposed = true;
        Stop();

        // Delete port mapping
        if (_hasPortMapping) {
            _upnpClient.DeletePortMapping(_portMapping);
            _hasPortMapping = false;
        }

        // Unregister handlers
        if (_masterProtocol) {
            _masterProtocol->onExternalProxyToken = nullptr;
            _masterProtocol->onExternalProxyState = nullptr;
        }

        LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Disposed");
    }

    void P2pProxyServer::Configure(const ProxyConfig& config) {
        _broadcastAddress = config.proxyIp | (~config.proxySubnetMask);
        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Configured broadcast=0x%08x", _broadcastAddress);
    }

    u16 P2pProxyServer::NatPunch() {
        // UPnP is optional - if it fails, we fallback to server relay
        // No try-catch needed (exceptions disabled), failures return 0
        
        // Discover UPnP device
        if (!_upnpClient.DiscoverDevice()) {
            LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: UPnP device not found");
            return 0;
        }

        LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: UPnP device discovered");

        // Try to create port mapping with different public ports
        _publicPort = PublicPortBase;

        for (s32 i = 0; i < PublicPortRange; i++) {
            _portMapping = PortMapping(
                UpnpProtocol::TCP,
                _privatePort,
                _publicPort,
                PortLeaseLength,
                "RyuLDN Local Multiplayer"
            );

            if (_upnpClient.CreatePortMapping(_portMapping)) {
                _hasPortMapping = true;
                LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Port mapping created %u -> %u", _privatePort, _publicPort);

                // Start lease renewal thread with explicit nothrow allocation
                _leaseThreadStack.reset(new (std::nothrow) u8[LeaseThreadStackSize + os::ThreadStackAlignment]);
                
                if (!_leaseThreadStack) {
                    LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Failed to allocate lease thread stack");
                    // Continue without renewal thread - mapping will expire after lease time
                } else {
                    void* leaseStackTop = reinterpret_cast<void*>(util::AlignUp(reinterpret_cast<uintptr_t>(_leaseThreadStack.get()), os::ThreadStackAlignment));

                    // Create lease renewal thread - reinitialize to ensure clean state
                    std::memset(&_leaseThread, 0, sizeof(_leaseThread));
                    Result rc = os::CreateThread(&_leaseThread, LeaseRenewalThreadFunc, this, leaseStackTop, LeaseThreadStackSize, 0x2C, 3);
                    if (R_SUCCEEDED(rc)) {
                        _leaseThreadRunning = true;
                        os::StartThread(&_leaseThread);
                    }
                }

                return _publicPort;
            }

            // Fast-fail on hard HTTP errors (e.g. 404 Not Found means IGD rejects AddPortMapping)
            const int lastStatus = _upnpClient.GetLastHttpStatus();
            if (lastStatus == 404) {
                LOG_WARN_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Port mapping rejected with HTTP %d - stopping attempts", lastStatus);
                break;
            }

            _publicPort++;
        }

        LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Failed to create port mapping");
        _publicPort = 0;
        return 0;
    }

    void P2pProxyServer::AcceptThreadFunc(void* arg) {
        P2pProxyServer* server = static_cast<P2pProxyServer*>(arg);
        server->AcceptLoop();
    }

    void P2pProxyServer::AcceptLoop() {
        while (_running) {
            sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);

            s32 clientSocket = accept(_listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);

            if (clientSocket < 0) {
                if (_running) {
                    LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Accept error: %d", errno);
                }
                break;
            }

            // Create or reuse session from pool
            LOG_HEAP(COMP_RLDN_P2P_SRV, "before SessionPool Acquire");
            P2pProxySession* session = _sessionPool.Acquire(clientSocket);
            if (session == nullptr) {
                LOG_INFO(COMP_RLDN_P2P_SRV, "ERROR: Failed to acquire session from pool - pool exhausted or out of memory");
                LOG_HEAP(COMP_RLDN_P2P_SRV, "after SessionPool Acquire FAILED");
                close(clientSocket);
                continue;
            }
            LOG_HEAP(COMP_RLDN_P2P_SRV, "after SessionPool Acquire");

            if (!session->Start()) {
                LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Failed to start session");
                _sessionPool.Release(session);
                continue;
            }

            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Client connected from %s:%u", ipStr, ntohs(clientAddr.sin_port));
        }
    }

    void P2pProxyServer::LeaseRenewalThreadFunc(void* arg) {
        P2pProxyServer* server = static_cast<P2pProxyServer*>(arg);
        server->LeaseRenewalLoop();
    }

    void P2pProxyServer::LeaseRenewalLoop() {
        while (_leaseThreadRunning && !_disposed) {
            // Wait for renewal interval
            os::SleepThread(TimeSpan::FromSeconds(PortLeaseRenew));

            if (!_leaseThreadRunning || _disposed) {
                break;
            }

            // Refresh lease
            if (!RefreshLease()) {
                LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Failed to refresh port mapping lease");
            }
        }
    }

    bool P2pProxyServer::RefreshLease() {
        if (!_hasPortMapping) {
            return false;
        }

        if (_upnpClient.CreatePortMapping(_portMapping)) {
            LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Port mapping lease renewed");
            return true;
        }

        return false;
    }

    void P2pProxyServer::HandleToken([[maybe_unused]] const LdnHeader& header, const ExternalProxyToken& token) {
        std::scoped_lock lk(_tokensLock);
        _waitingTokens.push_back(token);
        os::SignalSystemEvent(_tokenEvent.GetBase());

        LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Token received for virtual IP 0x%08x", token.virtualIp);
    }

    void P2pProxyServer::HandleStateChange([[maybe_unused]] const LdnHeader& header, const ExternalProxyConnectionState& state) {
        if (!state.connected) {
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: State change - disconnecting 0x%08x", state.ipAddress);

            // Remove waiting tokens for this IP
            {
                std::scoped_lock lk(_tokensLock);
                _waitingTokens.erase(
                    std::remove_if(_waitingTokens.begin(), _waitingTokens.end(),
                                  [&state](const ExternalProxyToken& token) {
                                      return token.virtualIp == state.ipAddress;
                                  }),
                    _waitingTokens.end());
            }

            // Disconnect and remove session
            {
                std::scoped_lock lk(_playersLock);
                auto it = _players.find(state.ipAddress);
                if (it != _players.end()) {
                    P2pProxySession* session = it->second;
                    session->DisconnectAndStop();
                    _sessionPool.Release(session);  // Return to pool
                    _players.erase(it);
                }
            }
        }
    }

    template<typename TMessage>
    void P2pProxyServer::RouteMessage(P2pProxySession* sender, TMessage& message,
                                      std::function<void(P2pProxySession*)> action) {
        ProxyInfo& info = message.info;

        // Validate source IP
        if (info.sourceIpV4 == 0) {
            // If they sent from a connection bound on 0.0.0.0, make others see it as them
            info.sourceIpV4 = sender->GetVirtualIpAddress();
        } else if (info.sourceIpV4 != sender->GetVirtualIpAddress()) {
            // Can't pretend to be somebody else
            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Rejected spoofed packet from 0x%08x claiming to be 0x%08x",
                     sender->GetVirtualIpAddress(), info.sourceIpV4);
            return;
        }

        u32 destIp = info.destIpV4;

        // Handle hardcoded broadcast address (192.168.0.255)
        if (destIp == 0xc0a800ff) {
            destIp = _broadcastAddress;
        }

        bool isBroadcast = (destIp == _broadcastAddress);

        std::scoped_lock lk(_playersLock);

        if (isBroadcast) {
            // Send to all players
            for (auto& pair : _players) {
                action(pair.second);
            }
        } else {
            // Send to specific player
            auto it = _players.find(destIp);
            if (it != _players.end()) {
                action(it->second);
            }
        }
    }

    void P2pProxyServer::HandleProxyDisconnect(P2pProxySession* sender, [[maybe_unused]] const LdnHeader& header, const ProxyDisconnectMessageFull& message) {
        ProxyDisconnectMessageFull msg = message;
        RouteMessage(sender, msg, [&](P2pProxySession* target) {
            ScopedBuffer buffer(g_sharedBufferPool);
            if (!buffer.Get()) return;
            int packetSize = RyuLdnProtocol::Encode(PacketId::ProxyDisconnect, msg, buffer.Get());
            target->SendAsync(buffer.Get(), packetSize);
        });
    }

    void P2pProxyServer::HandleProxyData(P2pProxySession* sender, [[maybe_unused]] const LdnHeader& header, const ProxyDataHeaderFull& message, const u8* data, u32 dataSize) {
        ProxyDataHeaderFull msg = message;
        RouteMessage(sender, msg, [&](P2pProxySession* target) {
            ScopedBuffer buffer(g_sharedBufferPool);
            if (!buffer.Get()) return;
            int packetSize = RyuLdnProtocol::Encode(PacketId::ProxyData, msg, data, dataSize, buffer.Get());
            target->SendAsync(buffer.Get(), packetSize);
        });
    }

    void P2pProxyServer::HandleProxyConnectReply(P2pProxySession* sender, [[maybe_unused]] const LdnHeader& header, const ProxyConnectResponseFull& message) {
        ProxyConnectResponseFull msg = message;
        RouteMessage(sender, msg, [&](P2pProxySession* target) {
            ScopedBuffer buffer(g_sharedBufferPool);
            if (!buffer.Get()) return;
            int packetSize = RyuLdnProtocol::Encode(PacketId::ProxyConnectReply, msg, buffer.Get());
            target->SendAsync(buffer.Get(), packetSize);
        });
    }

    void P2pProxyServer::HandleProxyConnect(P2pProxySession* sender, [[maybe_unused]] const LdnHeader& header, const ProxyConnectRequestFull& message) {
        ProxyConnectRequestFull msg = message;
        RouteMessage(sender, msg, [&](P2pProxySession* target) {
            ScopedBuffer buffer(g_sharedBufferPool);
            if (!buffer.Get()) return;
            int packetSize = RyuLdnProtocol::Encode(PacketId::ProxyConnect, msg, buffer.Get());
            target->SendAsync(buffer.Get(), packetSize);
        });
    }

    bool P2pProxyServer::TryRegisterUser(P2pProxySession* session, const ExternalProxyConfig& config) {
        // Get client's remote address
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        if (getpeername(session->GetSocket(), reinterpret_cast<sockaddr*>(&clientAddr), &addrLen) < 0) {
            LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: Failed to get client address");
            return false;
        }

        // Convert address to bytes for comparison
        u8 addressBytes[16];
        std::memset(addressBytes, 0, sizeof(addressBytes));

        if (clientAddr.sin_family == AF_INET) {
            // IPv4 - map to IPv6 format
            std::memcpy(addressBytes + 12, &clientAddr.sin_addr.s_addr, 4);
        }

        // Try to find matching token with timeout
        os::Tick startTime = os::GetSystemTick();
        os::Tick timeoutTicks = os::Tick(os::GetSystemTickFrequency() * AuthWaitSeconds);

        do {
            std::scoped_lock lk(_tokensLock);

            for (size_t i = 0; i < _waitingTokens.size(); i++) {
                const ExternalProxyToken& waitToken = _waitingTokens[i];

                // Check if this is a private IP token (all zeros)
                bool isPrivate = true;
                for (size_t j = 0; j < 16; j++) {
                    if (waitToken.physicalIp[j] != 0) {
                        isPrivate = false;
                        break;
                    }
                }

                // Check IP match
                bool ipEqual = isPrivate || (waitToken.addressFamily == static_cast<u8>(clientAddr.sin_family) &&
                                            std::memcmp(waitToken.physicalIp, addressBytes, 16) == 0);

                // Check token match
                bool tokenEqual = std::memcmp(waitToken.token, config.token, 16) == 0;

                if (ipEqual && tokenEqual) {
                    // Match found!
                    _waitingTokens.erase(_waitingTokens.begin() + i);

                    session->SetIpv4(waitToken.virtualIp);

                    ProxyConfig pconfig;
                    pconfig.proxyIp = session->GetVirtualIpAddress();
                    pconfig.proxySubnetMask = 0xFFFF0000; // TODO: Use from server

                    // Configure broadcast on first player
                    {
                        std::scoped_lock playersLk(_playersLock);
                        if (_players.empty()) {
                            Configure(pconfig);
                        }
                        _players[waitToken.virtualIp] = session;
                    }

                    // Send proxy config to client
                    ScopedBuffer buffer(g_sharedBufferPool);
                    if (buffer.Get()) {
                        int packetSize = RyuLdnProtocol::Encode(PacketId::ProxyConfig, pconfig, buffer.Get());
                        session->SendAsync(buffer.Get(), packetSize);
                    }

                    LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: User registered with virtual IP 0x%08x", waitToken.virtualIp);
                    return true;
                }
            }

            // Couldn't find token, wait for new tokens
            os::Tick currentTime = os::GetSystemTick();
            os::Tick elapsedTicks = currentTime - startTime;

            if (elapsedTicks >= timeoutTicks) {
                break; // Timeout
            }

            // Calculate remaining wait time
            os::Tick remainingTicks = timeoutTicks - elapsedTicks;
            TimeSpan waitTime = TimeSpan::FromNanoSeconds(os::ConvertToTimeSpan(remainingTicks).GetNanoSeconds());

            // Wait for token event
            os::TimedWaitSystemEvent(_tokenEvent.GetBase(), waitTime);
            os::ClearSystemEvent(_tokenEvent.GetBase());

        } while (true);

        LOG_INFO(COMP_RLDN_P2P_SRV, "P2pProxyServer: User registration failed - no matching token");
        return false;
    }

    void P2pProxyServer::DisconnectProxyClient(P2pProxySession* session) {
        bool removed = false;
        u32 virtualIp = session->GetVirtualIpAddress();

        {
            std::scoped_lock lk(_playersLock);
            auto it = _players.find(virtualIp);
            if (it != _players.end() && it->second == session) {
                _players.erase(it);
                removed = true;
            }
        }

        if (removed) {
            // Return session to pool for reuse
            _sessionPool.Release(session);
            
            // Notify master server of disconnection
            ExternalProxyConnectionState state;
            state.ipAddress = virtualIp;
            state.connected = false;

            ScopedBuffer buffer(g_sharedBufferPool);
            if (buffer.Get()) {
                int packetSize = RyuLdnProtocol::Encode(PacketId::ExternalProxyState, state, buffer.Get());
                _master->SendRawPacket(buffer.Get(), packetSize);
            }

            LOG_INFO_ARGS(COMP_RLDN_P2P_SRV, "P2pProxyServer: Client disconnected (virtual IP 0x%08x)", session->GetVirtualIpAddress());
        }
    }

} // namespace ams::mitm::ldn::ryuldn::proxy
