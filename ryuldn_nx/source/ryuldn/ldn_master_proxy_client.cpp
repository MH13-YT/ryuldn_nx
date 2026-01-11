#include "ldn_master_proxy_client.hpp"
#include "../debug.hpp"


#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <cstring>
#include <algorithm>
#include <netinet/tcp.h>

namespace ams::mitm::ldn::ryuldn {

LdnMasterProxyClient::LdnMasterProxyClient(const char* serverAddress, int serverPort, bool useP2pProxy)
    : _serverAddress(serverAddress), 
      _serverPort(serverPort), 
      _useP2pProxy(useP2pProxy),
      _packetBufferMutex(false) 
{

    _socket = -1;
    _connected = false;
    _networkConnected = false;
    _stop = false;
    _hostedProxy = nullptr;
    _connectedProxy = nullptr;
    _disconnectReason = DisconnectReason::None;
    _lastError = NetworkError::None;

    _threadStack = std::make_unique<u8[]>(ThreadStackSize + os::ThreadStackAlignment);
    _packetBuffer = std::make_unique<u8[]>(MaxPacketSize);

    // Allocate SystemEvent objects using operator new (which uses our custom heap)
    // We allocate raw memory first, then use placement new to construct
    _connectedEvent = static_cast<os::SystemEvent*>(::operator new(sizeof(os::SystemEvent), std::nothrow));
    _errorEvent     = static_cast<os::SystemEvent*>(::operator new(sizeof(os::SystemEvent), std::nothrow));
    _scanEvent      = static_cast<os::SystemEvent*>(::operator new(sizeof(os::SystemEvent), std::nothrow));
    _rejectEvent    = static_cast<os::SystemEvent*>(::operator new(sizeof(os::SystemEvent), std::nothrow));
    _apConnectedEvent = static_cast<os::SystemEvent*>(::operator new(sizeof(os::SystemEvent), std::nothrow));

    // Verify allocations succeeded
    if (!_connectedEvent || !_errorEvent || !_scanEvent || !_rejectEvent || !_apConnectedEvent) {
        // Clean up any successful allocations
        if (_connectedEvent) ::operator delete(_connectedEvent);
        if (_errorEvent) ::operator delete(_errorEvent);
        if (_scanEvent) ::operator delete(_scanEvent);
        if (_rejectEvent) ::operator delete(_rejectEvent);
        if (_apConnectedEvent) ::operator delete(_apConnectedEvent);

        _connectedEvent = _errorEvent = _scanEvent = _rejectEvent = _apConnectedEvent = nullptr;
        AMS_ABORT("Failed to allocate SystemEvent objects");
    }

    // Initialize SystemEvents using placement new
    new (_connectedEvent) os::SystemEvent(os::EventClearMode_ManualClear, false);
    new (_errorEvent) os::SystemEvent(os::EventClearMode_ManualClear, false);
    new (_scanEvent) os::SystemEvent(os::EventClearMode_ManualClear, false);
    new (_rejectEvent) os::SystemEvent(os::EventClearMode_ManualClear, false);
    new (_apConnectedEvent) os::SystemEvent(os::EventClearMode_AutoClear, false);

    // Initialize to all zeros (server will assign ID and MAC in response)
    // This matches Ryujinx behavior - see InitializeMessage.cs comments:
    // "All 0 if we don't have an ID yet" and "All 0 if we don't have a mac yet"
    std::memset(&_initializeMemory, 0, sizeof(_initializeMemory));
    std::memset(_gameVersion, 0, sizeof(_gameVersion));

    _protocol.onInitialize = [this](const LdnHeader& h, const InitializeMessage& m) { HandleInitialize(h, m); };
    _protocol.onConnected = [this](const LdnHeader& h, const NetworkInfo& i) { HandleConnected(h, i); };
    _protocol.onSyncNetwork = [this](const LdnHeader& h, const NetworkInfo& i) { HandleSyncNetwork(h, i); };
    _protocol.onDisconnected = [this](const LdnHeader& h, const DisconnectMessage& m) { HandleDisconnected(h, m); };
    _protocol.onRejectReply = [this](const LdnHeader& h) { HandleRejectReply(h); };
    _protocol.onScanReply = [this](const LdnHeader& h, const NetworkInfo& i) { HandleScanReply(h, i); };
    _protocol.onScanReplyEnd = [this](const LdnHeader& h) { HandleScanReplyEnd(h); };
    _protocol.onProxyConfig = [this](const LdnHeader& h, const ProxyConfig& c) { HandleProxyConfig(h, c); };
    _protocol.onProxyData = [this](const LdnHeader& h, const ProxyDataHeaderFull& hdr, const u8* p, u32 s) { 
        HandleProxyData(h, hdr, p, s); 
    };
    _protocol.onPing = [this](const LdnHeader& h, const PingMessage& p) { HandlePing(h, p); };
    _protocol.onNetworkError = [this](const LdnHeader& h, const NetworkErrorMessage& e) { HandleNetworkError(h, e); };
    _protocol.onExternalProxy = [this](const LdnHeader& h, const ExternalProxyConfig& c) { HandleExternalProxy(h, c); };
}

LdnMasterProxyClient::~LdnMasterProxyClient() {
    static_cast<void>(Finalize());

    // Manually destroy and deallocate SystemEvents
    if (_connectedEvent) {
        _connectedEvent->~SystemEvent();
        ::operator delete(_connectedEvent);
    }
    if (_errorEvent) {
        _errorEvent->~SystemEvent();
        ::operator delete(_errorEvent);
    }
    if (_scanEvent) {
        _scanEvent->~SystemEvent();
        ::operator delete(_scanEvent);
    }
    if (_rejectEvent) {
        _rejectEvent->~SystemEvent();
        ::operator delete(_rejectEvent);
    }
    if (_apConnectedEvent) {
        _apConnectedEvent->~SystemEvent();
        ::operator delete(_apConnectedEvent);
    }

    if (_hostedProxy) delete _hostedProxy;
    if (_connectedProxy) delete _connectedProxy;
}

Result LdnMasterProxyClient::Initialize() {
    if (!_packetBuffer || !_threadStack) return MAKERESULT(0xFD, 1);
    
    // Initialize timeout handler
    _timeout = std::make_unique<NetworkTimeout>(InactiveTimeout, [this]() {
        this->TimeoutConnection();
    });
    
    uintptr_t stackAddr = reinterpret_cast<uintptr_t>(_threadStack.get());
    uintptr_t alignedAddr = util::AlignUp(stackAddr, os::ThreadStackAlignment);
    void* stackTop = reinterpret_cast<void*>(alignedAddr);
    Result rc = os::CreateThread(&_workerThread, WorkerThreadFunc, this, stackTop, ThreadStackSize, 0x15, 2);
    if (R_FAILED(rc)) return rc;
    os::StartThread(&_workerThread);
    return ResultSuccess();
}

Result LdnMasterProxyClient::Finalize() {
    if (_stop) return ResultSuccess();
    _stop = true;
    if (_socket >= 0) Disconnect();
    
    // Cleanup timeout
    if (_timeout) {
        _timeout->Dispose();
        _timeout.reset();
    }
    
    os::WaitThread(&_workerThread);
    os::DestroyThread(&_workerThread);
    return ResultSuccess();
}

void LdnMasterProxyClient::WorkerThreadFunc(void* arg) {
    LdnMasterProxyClient* client = static_cast<LdnMasterProxyClient*>(arg);
    if (client) client->WorkerLoop();
}

void LdnMasterProxyClient::WorkerLoop() {
    while (!_stop) {
        if (_connected && ReceiveData() < 0) {
            Disconnect();
            _errorEvent->Signal();
        }
        
        // Check for timeouts periodically
        if (_timeout) {
            _timeout->CheckTimeout();
        }
        
        os::SleepThread(TimeSpan::FromMilliSeconds(10));
    }
}

bool LdnMasterProxyClient::EnsureConnected() {
    if (_connected) return true;
    LOG_DBG_ARGS(COMP_SVC," EnsureConnected: Attempting connection to %s:%d", _serverAddress.c_str(), _serverPort);
    _errorEvent->Clear();
    _connectedEvent->Clear();
    _socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_socket < 0) {
        LOG_ERR_ARGS(COMP_SVC,"EnsureConnected: socket() failed, errno=%d", errno);
        return false;
    }
    LOG_DBG_ARGS(COMP_SVC," EnsureConnected: Socket created (fd=%d)", _socket);
    int flags = fcntl(_socket, F_GETFL, 0);
    fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
    struct addrinfo hints, *result;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", _serverPort);
    LOG_DBG_ARGS(COMP_SVC," EnsureConnected: Calling getaddrinfo(%s, %s)", _serverAddress.c_str(), portStr);
    if (getaddrinfo(_serverAddress.c_str(), portStr, &hints, &result) != 0) {
        LOG_ERR_ARGS(COMP_SVC,"EnsureConnected: getaddrinfo() failed, errno=%d", errno);
        close(_socket); _socket = -1; return false;
    }
    LOG_DBG(COMP_SVC," EnsureConnected: DNS resolved, attempting connect");
    int rc = ::connect(_socket, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    if (rc < 0 && errno != EINPROGRESS) {
        LOG_ERR_ARGS(COMP_SVC,"EnsureConnected: connect() failed, errno=%d", errno);
        close(_socket); _socket = -1; return false;
    }
    LOG_DBG_ARGS(COMP_SVC," EnsureConnected: Connection in progress, polling (timeout=%dms)", FailureTimeout);
    struct pollfd pfd;
    pfd.fd = _socket; pfd.events = POLLOUT;
    int poll_result = poll(&pfd, 1, FailureTimeout);
    if (poll_result <= 0) {
        LOG_ERR_ARGS(COMP_SVC,"EnsureConnected: poll() %s, result=%d, errno=%d",
                  poll_result == 0 ? "timed out" : "failed", poll_result, errno);
        close(_socket); _socket = -1; return false;
    }
    LOG_DBG(COMP_SVC," EnsureConnected: Poll succeeded, checking socket error");
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(_socket, SOL_SOCKET, SO_ERROR, &error, &len);
    if (error != 0) {
        LOG_ERR_ARGS(COMP_SVC,"EnsureConnected: Socket error after connect: %d", error);
        close(_socket); _socket = -1; return false;
    }
    LOG_INFO(COMP_SVC,"EnsureConnected: Successfully connected to server!");

    // Switch socket back to blocking mode for reliable data transmission
    flags = fcntl(_socket, F_GETFL, 0);
    fcntl(_socket, F_SETFL, flags & ~O_NONBLOCK);
    LOG_DBG(COMP_SVC," Switched socket to blocking mode");

    // Enable TCP_NODELAY to disable Nagle's algorithm (send packets immediately)
    int nodelay = 1;
    if (setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) == 0) {
        LOG_DBG(COMP_SVC," TCP_NODELAY enabled");
    } else {
        LOG_WARN_ARGS(COMP_SVC,"Failed to enable TCP_NODELAY, errno=%d", errno);
    }

    _connected = true;
    _protocol.Reset();
    _connectedEvent->Signal();

    // Give the server a moment to be ready
    os::SleepThread(TimeSpan::FromMilliSeconds(100));

    std::scoped_lock lock(_packetBufferMutex);

    // Debug: Log _initializeMemory content
    LOG_DBG(COMP_SVC," _initializeMemory content:");
    LOG_DBG_ARGS(COMP_SVC,"   ID: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
              _initializeMemory.id[0], _initializeMemory.id[1], _initializeMemory.id[2], _initializeMemory.id[3],
              _initializeMemory.id[4], _initializeMemory.id[5], _initializeMemory.id[6], _initializeMemory.id[7],
              _initializeMemory.id[8], _initializeMemory.id[9], _initializeMemory.id[10], _initializeMemory.id[11],
              _initializeMemory.id[12], _initializeMemory.id[13], _initializeMemory.id[14], _initializeMemory.id[15]);
    LOG_DBG_ARGS(COMP_SVC,"   MAC: %02X:%02X:%02X:%02X:%02X:%02X",
              _initializeMemory.macAddress[0], _initializeMemory.macAddress[1], _initializeMemory.macAddress[2],
              _initializeMemory.macAddress[3], _initializeMemory.macAddress[4], _initializeMemory.macAddress[5]);

    // Debug: Verify structure sizes
    LOG_DBG_ARGS(COMP_SVC," sizeof(LdnHeader)=%zu, sizeof(InitializeMessage)=%zu",
              sizeof(LdnHeader), sizeof(InitializeMessage));

    int size = RyuLdnProtocol::Encode(PacketId::Initialize, _initializeMemory, _packetBuffer.get());

    // Debug: Log the packet being sent
    LOG_DBG_ARGS(COMP_SVC," Sending Initialize packet: size=%d bytes", size);
    LOG_DBG(COMP_SVC," Full packet dump (first 32 bytes):");
    for (int i = 0; i < 32 && i < size; i += 16) {
        int remaining = size - i;
        if (remaining >= 16) {
            LOG_DBG_ARGS(COMP_SVC,"   [%02d-%02d]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                      i, i+15,
                      _packetBuffer[i+0], _packetBuffer[i+1], _packetBuffer[i+2], _packetBuffer[i+3],
                      _packetBuffer[i+4], _packetBuffer[i+5], _packetBuffer[i+6], _packetBuffer[i+7],
                      _packetBuffer[i+8], _packetBuffer[i+9], _packetBuffer[i+10], _packetBuffer[i+11],
                      _packetBuffer[i+12], _packetBuffer[i+13], _packetBuffer[i+14], _packetBuffer[i+15]);
        }
    }

    int sent = SendPacket(_packetBuffer.get(), size);
    LOG_DBG_ARGS(COMP_SVC," SendPacket returned: %d (expected %d)", sent, size);
    if (sent != size) {
        LOG_ERR(COMP_SVC,"Failed to send complete packet!");
        _connected = false;
        return false;
    }
    return true;
}

void LdnMasterProxyClient::Disconnect() {
    if (_socket >= 0) { close(_socket); _socket = -1; }
    _connected = false;
    DisconnectInternal();
}

void LdnMasterProxyClient::DisconnectInternal() {
    if (_networkConnected) {
        _networkConnected = false;
        if (_hostedProxy) { delete _hostedProxy; _hostedProxy = nullptr; }
        if (_connectedProxy) { delete _connectedProxy; _connectedProxy = nullptr; }
        
        // Refresh timeout when disconnecting from network (Ryujinx behavior)
        if (_timeout) {
            _timeout->RefreshTimeout();
        }
    }
}

int LdnMasterProxyClient::SendPacket(const u8* data, int size) {
    if (!_connected || _socket < 0) {
        LOG_ERR_ARGS(COMP_SVC,"SendPacket: Not connected (_connected=%d, _socket=%d)", _connected, _socket);
        return -1;
    }
    LOG_DBG_ARGS(COMP_SVC," SendPacket: Sending %d bytes", size);
    
    // Hex dump for first 64 bytes
    if (size > 0 && size <= 64) {
        char hexdump[256] = {0};
        int hexpos = 0;
        for (int i = 0; i < size && hexpos < 250; i++) {
            hexpos += snprintf(hexdump + hexpos, sizeof(hexdump) - hexpos, "%02X ", data[i]);
        }
        LOG_DBG_ARGS(COMP_SVC," SendPacket hex dump (%d bytes): %s", size, hexdump);
    }
    
    std::lock_guard<std::mutex> lock(_sendMutex);
    int sent = 0;
    while (sent < size) {
        int rc = ::send(_socket, data + sent, size - sent, 0);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_DBG_ARGS(COMP_SVC," SendPacket: EAGAIN/EWOULDBLOCK, retrying... (sent %d/%d)", sent, size);
                os::SleepThread(TimeSpan::FromMilliSeconds(1));
                continue;
            }
            LOG_ERR_ARGS(COMP_SVC,"SendPacket: send() failed, errno=%d (sent %d/%d)", errno, sent, size);
            return -1;
        }
        sent += rc;
        LOG_DBG_ARGS(COMP_SVC," SendPacket: Sent %d bytes (total %d/%d)", rc, sent, size);
    }
    LOG_DBG_ARGS(COMP_SVC," SendPacket: Successfully sent all %d bytes", sent);
    return sent;
}

int LdnMasterProxyClient::SendRawPacket(const u8* data, int size) { return SendPacket(data, size); }

int LdnMasterProxyClient::ReceiveData() {
    if (!_connected || _socket < 0) {
        LOG_ERR_ARGS(COMP_SVC,"ReceiveData: Not connected (_connected=%d, _socket=%d)", _connected, _socket);
        return -1;
    }
    u8 buffer[4096];
    int received = ::recv(_socket, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (received < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            LOG_DBG_ARGS(COMP_SVC," ReceiveData: EAGAIN/EWOULDBLOCK (errno=%d)", err);
            return 0;
        }
        LOG_ERR_ARGS(COMP_SVC,"ReceiveData: recv() failed, errno=%d", err);
        return -1;
    }
    if (received == 0) {
        LOG_WARN(COMP_SVC,"ReceiveData: Connection closed by server (received 0 bytes)");
        return -1;
    }
    LOG_DBG_ARGS(COMP_SVC," ReceiveData: Received %d bytes, processing protocol", received);
    
    // Hex dump for debugging
    if (received > 0 && received <= 64) {
        char hexdump[256] = {0};
        int hexpos = 0;
        for (int i = 0; i < received && hexpos < 250; i++) {
            hexpos += snprintf(hexdump + hexpos, sizeof(hexdump) - hexpos, "%02X ", buffer[i]);
        }
        LOG_DBG_ARGS(COMP_SVC," ReceiveData hex dump (%d bytes): %s", received, hexdump);
    }
    
    _protocol.Read(buffer, 0, received);
    return received;
}

void LdnMasterProxyClient::TimeoutConnection() {
    _connected = false;
    Disconnect();
    while (_socket >= 0) {
        os::SleepThread(TimeSpan::FromMilliSeconds(10));
    }
}

void LdnMasterProxyClient::UpdatePassphraseIfNeeded(const char* passphrase) {
    if (!passphrase || _passphrase == passphrase) return;
    _passphrase = passphrase;
    if (_connected) {
        std::scoped_lock lock(_packetBufferMutex);
        PassphraseMessage msg{};
        strncpy(msg.passphrase, passphrase, sizeof(msg.passphrase) - 1);
        int size = RyuLdnProtocol::Encode(PacketId::Passphrase, msg, _packetBuffer.get());
        SendPacket(_packetBuffer.get(), size);
    }
}

void LdnMasterProxyClient::SetNetworkChangeCallback(NetworkChangeCallback cb) { _networkChangeCallback = cb; }
void LdnMasterProxyClient::SetProxyConfigCallback(ProxyConfigCallback cb) { _proxyConfigCallback = cb; }
void LdnMasterProxyClient::SetProxyDataCallback(ProxyDataCallback cb) { _proxyDataCallback = cb; }
void LdnMasterProxyClient::SetGameVersion(const u8* v, size_t s) { std::memcpy(_gameVersion, v, std::min(s, sizeof(_gameVersion))); }
void LdnMasterProxyClient::SetPassphrase(const char* p) { UpdatePassphraseIfNeeded(p); }

void LdnMasterProxyClient::HandleInitialize(const LdnHeader&, const InitializeMessage& m) { _initializeMemory = m; }
void LdnMasterProxyClient::HandleConnected(const LdnHeader&, const NetworkInfo& i) {
    LOG_INFO(COMP_SVC,"HandleConnected: Network connected");
    _networkConnected = true; _apConnectedEvent->Signal();
    if (_networkChangeCallback) {
        LOG_DBG(COMP_SVC," HandleConnected: Calling network change callback");
        _networkChangeCallback(i, true);
    }
}
void LdnMasterProxyClient::HandleSyncNetwork(const LdnHeader&, const NetworkInfo& i) {
    if (_networkChangeCallback) _networkChangeCallback(i, true);
}
void LdnMasterProxyClient::HandleDisconnected(const LdnHeader&, const DisconnectMessage& msg) { 
    LOG_WARN_ARGS(COMP_SVC,"HandleDisconnected: Received disconnect message, reason=%u", msg.reason);
    _disconnectReason = msg.reason;
    DisconnectInternal(); 
}
void LdnMasterProxyClient::HandleRejectReply(const LdnHeader&) { _rejectEvent->Signal(); }
void LdnMasterProxyClient::HandleScanReply(const LdnHeader&, const NetworkInfo& i) { _availableGames.push_back(i); }
void LdnMasterProxyClient::HandleScanReplyEnd(const LdnHeader&) { _scanEvent->Signal(); }
void LdnMasterProxyClient::HandleProxyConfig(const LdnHeader& h, const ProxyConfig& c) { 
    _config = c; if (_proxyConfigCallback) _proxyConfigCallback(h, c); 
}
void LdnMasterProxyClient::HandleProxyData(const LdnHeader& h, const ProxyDataHeaderFull& hdr, const u8* p, u32 s) {
    if (_proxyDataCallback) _proxyDataCallback(h, hdr, p, s);
}
void LdnMasterProxyClient::HandlePing(const LdnHeader&, const PingMessage& p) {
    if (p.requester == 0) {
        std::scoped_lock lock(_packetBufferMutex);
        int size = RyuLdnProtocol::Encode(PacketId::Ping, p, _packetBuffer.get());
        SendPacket(_packetBuffer.get(), size);
    }
}
void LdnMasterProxyClient::HandleNetworkError(const LdnHeader&, const NetworkErrorMessage& e) {
    if (e.error == NetworkError::PortUnreachable) _useP2pProxy = false;
    else _lastError = e.error;
}

void LdnMasterProxyClient::HandleExternalProxy(const LdnHeader&, const ExternalProxyConfig& config) {
    char ipStr[INET_ADDRSTRLEN];
    if (config.addressFamily == AF_INET) {
        struct in_addr addr; std::memcpy(&addr.s_addr, config.proxyIp + 12, 4);
        inet_ntop(AF_INET, &addr, ipStr, sizeof(ipStr));
    } else return;
    proxy::P2pProxyClient* client = new (std::nothrow) proxy::P2pProxyClient(ipStr, config.proxyPort);
    if (!client) return;
    _connectedProxy = client;
    if (!client->Connect() || !client->PerformAuth(config)) { delete client; _connectedProxy = nullptr; }
}

NetworkError LdnMasterProxyClient::ConsumeNetworkError() {
    NetworkError res = _lastError; _lastError = NetworkError::None; return res;
}

Result LdnMasterProxyClient::CreateNetwork(const CreateAccessPointRequest& req, const u8* d, u16 s) {
    LOG_INFO_ARGS(COMP_SVC,"CreateNetwork: Starting network creation, SSID=%s, passphrase_size=%u, useP2pProxy=%d", 
              req.securityConfig.passphraseSize > 0 ? "***HIDDEN***" : "PUBLIC",
              req.securityConfig.passphraseSize,
              _useP2pProxy);
    
    if (_timeout) {
        LOG_DBG(COMP_SVC," CreateNetwork: Disabling timeout before creation");
        _timeout->DisableTimeout();
    }
    
    if (!EnsureConnected()) {
        LOG_ERR(COMP_SVC,"CreateNetwork: Failed to connect to master server");
        DisconnectProxy();
        return MAKERESULT(0xFD, 1);
    }
    
    LOG_DBG_ARGS(COMP_SVC," CreateNetwork: Configuring network with P2P proxy (_useP2pProxy=%d)", _useP2pProxy);
    DisconnectInternal();
    CreateAccessPointRequest mod = req;
    std::memcpy(mod.ryuNetworkConfig.gameVersion, _gameVersion, sizeof(_gameVersion));
    ConfigureAccessPoint(mod.ryuNetworkConfig);
    
    std::scoped_lock lock(_packetBufferMutex);
    int sz = RyuLdnProtocol::Encode(PacketId::CreateAccessPoint, mod, d, s, _packetBuffer.get());
    SendPacket(_packetBuffer.get(), sz);
    
    if (_apConnectedEvent->TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout))) {
        if (mod.ryuNetworkConfig.externalProxyPort != 0) {
            LOG_INFO_ARGS(COMP_SVC,"CreateNetwork: P2P proxy configured on external port %u", 
                     mod.ryuNetworkConfig.externalProxyPort);
        } else {
            LOG_INFO(COMP_SVC,"CreateNetwork: Using master server relay (no P2P proxy)");
        }
        
        // Send immediate NetworkChange event with dummy NetworkInfo (Ryujinx behavior)
        // This prevents games from crashing with uninitialized structures
        NetworkInfo dummyInfo = {};
        
        // Set basic network info from request
        dummyInfo.networkId.intentId.localCommunicationId = req.networkConfig.intentId.localCommunicationId;
        dummyInfo.networkId.intentId.sceneId = req.networkConfig.intentId.sceneId;
        dummyInfo.networkId.sessionId.high = 0x0001020304050607ULL;
        dummyInfo.networkId.sessionId.low = 0x08090A0B0C0D0E0FULL;
        
        // Set common network info
        dummyInfo.common.bssid.raw[0] = 0x00;
        dummyInfo.common.bssid.raw[1] = 0xA0;
        dummyInfo.common.bssid.raw[2] = 0xC9;
        dummyInfo.common.bssid.raw[3] = 0x52;
        dummyInfo.common.bssid.raw[4] = 0x00;
        dummyInfo.common.bssid.raw[5] = 0x00;
        
        dummyInfo.common.ssid.length = strlen("Nintendo_AP");
        std::strcpy(dummyInfo.common.ssid.raw, "Nintendo_AP");
        dummyInfo.common.channel = 6;
        dummyInfo.common.linkLevel = 100;
        dummyInfo.common.networkType = 0;  // Adhoc
        
        // Set LDN network info
        dummyInfo.ldn.nodeCountMax = req.networkConfig.nodeCountMax;
        dummyInfo.ldn.nodeCount = 1;  // Just the host
        dummyInfo.ldn.securityMode = 2;  // WEP
        dummyInfo.ldn.stationAcceptPolicy = 1;
        
        // Set host node info
        dummyInfo.ldn.nodes[0].nodeId = 0;
        dummyInfo.ldn.nodes[0].isConnected = 1;
        dummyInfo.ldn.nodes[0].ipv4Address = 0xC0A8001ULL;  // 192.168.0.1
        dummyInfo.ldn.nodes[0].macAddress = dummyInfo.common.bssid;
        dummyInfo.ldn.nodes[0].localCommunicationVersion = req.networkConfig.localCommunicationVersion;
        
        // Copy advertise data to host node's userName as per LDN spec
        if (s > 0 && s <= UserNameBytesMax) {
            std::memcpy(dummyInfo.ldn.nodes[0].userName, d, s);
            dummyInfo.ldn.nodes[0].userName[s] = '\0';
        } else {
            std::strcpy(dummyInfo.ldn.nodes[0].userName, "HOST");
        }
        
        // Store for NetworkChange callback delivery
        _lastNetworkInfo = dummyInfo;
        
        // Signal the network change event
        if (_networkChangeCallback) {
            _networkChangeCallback(dummyInfo, true);
            LOG_INFO(COMP_SVC,"CreateNetwork: Sent immediate NetworkChange event");
        }
        
        return ResultSuccess();
    }
    LOG_ERR(COMP_SVC,"CreateNetwork: Timeout waiting for access point creation");
    return MAKERESULT(0xFD, 2);
}

Result LdnMasterProxyClient::Connect(const ConnectRequest& req) {
    if (_timeout) {
        _timeout->DisableTimeout();
    }
    
    LOG_DBG(COMP_SVC," Connect: Attempting to connect to network");
    if (!EnsureConnected()) {
        LOG_ERR(COMP_SVC,"Connect: Failed to connect to master server");
        return MAKERESULT(0xFD, 1);
    }
    std::scoped_lock lock(_packetBufferMutex);
    int sz = RyuLdnProtocol::Encode(PacketId::Connect, req, _packetBuffer.get());
    SendPacket(_packetBuffer.get(), sz);
    if (_apConnectedEvent->TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout))) {
        LOG_INFO(COMP_SVC,"Connect: Successfully connected to network");
        return ResultSuccess();
    }
    LOG_ERR(COMP_SVC,"Connect: Timeout waiting for connection");
    return MAKERESULT(0xFD, 2);
}

Result LdnMasterProxyClient::Scan(NetworkInfo* nets, u16* count, const ScanFilter& f) {
    LOG_INFO(COMP_SVC,"Scan: Starting scan operation");
    // Reset timeout only if not connected to a network (Ryujinx behavior)
    if (!_networkConnected && _timeout) {
        LOG_DBG(COMP_SVC," Scan: Refreshing timeout (not network connected)");
        _timeout->RefreshTimeout();
    }

    // Clear previous results
    _availableGames.clear();
    
    if (!EnsureConnected()) { 
        LOG_ERR(COMP_SVC,"Scan: Failed to ensure connection to master server");
        *count = 0;
        return MAKERESULT(0xFD, 1); 
    }
    
    LOG_DBG(COMP_SVC," Scan: Connected, sending scan request");
    // Reset scan event and send scan request
    _scanEvent->Clear();
    {
        std::scoped_lock lock(_packetBufferMutex);
        int sz = RyuLdnProtocol::Encode(PacketId::Scan, f, _packetBuffer.get());
        LOG_DBG_ARGS(COMP_SVC," Scan: Encoded packet size=%d", sz);
        SendPacket(_packetBuffer.get(), sz);
    }
    
    LOG_DBG_ARGS(COMP_SVC," Scan: Waiting for scan results (timeout=%ums)", ScanTimeout);
    // Wait for scan completion (ScanReplyEnd) with timeout
    if (!_scanEvent->TimedWait(TimeSpan::FromMilliSeconds(ScanTimeout))) { 
        LOG_WARN_ARGS(COMP_SVC,"Scan: Timeout waiting for scan results after %ums", ScanTimeout);
        *count = 0;
        return ResultSuccess();  // Timeout is not an error
    }
    
    // Copy results
    u16 found = std::min(static_cast<u16>(_availableGames.size()), *count);
    for (u16 i = 0; i < found; i++) nets[i] = _availableGames[i];
    *count = found;
    LOG_INFO_ARGS(COMP_SVC,"Scan: Completed, found %u networks", found);
    return ResultSuccess();
}

Result LdnMasterProxyClient::DisconnectNetwork() {
    if (_networkConnected) {
        DisconnectMessage msg{DisconnectReason::DisconnectedByUser};
        std::scoped_lock lock(_packetBufferMutex);
        int sz = RyuLdnProtocol::Encode(PacketId::Disconnect, msg, _packetBuffer.get());
        SendPacket(_packetBuffer.get(), sz);
        DisconnectInternal();
    }
    return ResultSuccess();
}

Result LdnMasterProxyClient::SetAdvertiseData(const u8* d, u16 s) {
    if (!_networkConnected) return MAKERESULT(0xFD, 3);
    std::scoped_lock lock(_packetBufferMutex);
    int sz = RyuLdnProtocol::Encode(PacketId::SetAdvertiseData, d, s, _packetBuffer.get());
    SendPacket(_packetBuffer.get(), sz);
    return ResultSuccess();
}

Result LdnMasterProxyClient::SetStationAcceptPolicy(u8 p) {
    if (!_networkConnected) return MAKERESULT(0xFD, 3);
    SetAcceptPolicyRequest req{p};
    std::scoped_lock lock(_packetBufferMutex);
    int sz = RyuLdnProtocol::Encode(PacketId::SetAcceptPolicy, req, _packetBuffer.get());
    SendPacket(_packetBuffer.get(), sz);
    return ResultSuccess();
}

Result LdnMasterProxyClient::Reject(DisconnectReason reason, u32 nodeId) {
    if (!_networkConnected) return MAKERESULT(0xFD, 3);
    _rejectEvent->Clear();
    RejectRequest req;
    std::memset(&req, 0, sizeof(req));
    req.disconnectReason = reason;
    req.nodeId = nodeId;
    std::scoped_lock lock(_packetBufferMutex);
    int sz = RyuLdnProtocol::Encode(PacketId::Reject, req, _packetBuffer.get());
    SendPacket(_packetBuffer.get(), sz);
    if (_rejectEvent->TimedWait(TimeSpan::FromMilliSeconds(InactiveTimeout))) return ResultSuccess();
    return MAKERESULT(0xFD, 4);
}

Result LdnMasterProxyClient::CreateNetworkPrivate(const CreateAccessPointPrivateRequest& req, const u8* d, u16 s) {
    if (_timeout) {
        _timeout->DisableTimeout();
    }
    
    if (!EnsureConnected()) {
        DisconnectProxy();
        return MAKERESULT(0xFD, 1);
    }
    std::scoped_lock lock(_packetBufferMutex);
    int sz = RyuLdnProtocol::Encode(PacketId::CreateAccessPointPrivate, req, d, s, _packetBuffer.get());
    SendPacket(_packetBuffer.get(), sz);
    if (_apConnectedEvent->TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout))) return ResultSuccess();
    return MAKERESULT(0xFD, 2);
}

Result LdnMasterProxyClient::ConnectPrivate(const ConnectPrivateRequest& req) {
    if (_timeout) {
        _timeout->DisableTimeout();
    }
    
    if (!EnsureConnected()) return MAKERESULT(0xFD, 1);
    std::scoped_lock lock(_packetBufferMutex);
    int sz = RyuLdnProtocol::Encode(PacketId::ConnectPrivate, req, _packetBuffer.get());
    SendPacket(_packetBuffer.get(), sz);
    if (_apConnectedEvent->TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout))) return ResultSuccess();
    return MAKERESULT(0xFD, 2);
}

void LdnMasterProxyClient::ConfigureAccessPoint(RyuNetworkConfig& config) {
    if (!_useP2pProxy) {
        config.externalProxyPort = 0;
        config.internalProxyPort = 0;
        LOG_DBG(COMP_SVC," ConfigureAccessPoint: P2P proxy disabled");
        return;
    }
    
    // Attempt to create P2P proxy server on a port range
    for (u16 i = 0; i < proxy::P2pProxyServer::PrivatePortRange; i++) {
        u16 port = proxy::P2pProxyServer::PrivatePortBase + i;
        _hostedProxy = new proxy::P2pProxyServer(this, port, &_protocol);
        
        if (!_hostedProxy->Start()) {
            delete _hostedProxy;
            _hostedProxy = nullptr;
            continue;
        }
        
        LOG_DBG_ARGS(COMP_SVC," ConfigureAccessPoint: P2P proxy server started on port %u", port);
        
        // Successfully started proxy, attempt UPnP NAT punch
        u16 externalPort = _hostedProxy->NatPunch();
        if (externalPort != 0) {
            // UPnP successful - configure external proxy
            config.externalProxyPort = externalPort;
            config.internalProxyPort = port;
            config.addressFamily = 2;  // InterNetwork (IPv4)
            LOG_INFO_ARGS(COMP_SVC,"ConfigureAccessPoint: Created P2P proxy on external port %u, internal port %u", 
                     externalPort, port);
            return;
        }
        
        // UPnP failed, cleanup and fall back to server proxy
        LOG_WARN_ARGS(COMP_SVC,"ConfigureAccessPoint: UPnP NAT punch failed on port %u, trying next port", port);
        _hostedProxy->Stop();
        delete _hostedProxy;
        _hostedProxy = nullptr;
    }
    
    // All ports failed or no UPnP, fall back to server proxy relay
    config.externalProxyPort = 0;
    config.internalProxyPort = 0;
    LOG_WARN(COMP_SVC,"ConfigureAccessPoint: Could not open external P2P port, falling back to server relay");
}

void LdnMasterProxyClient::DisconnectProxy() {
    if (_hostedProxy) {
        _hostedProxy->Stop();
        delete _hostedProxy;
        _hostedProxy = nullptr;
    }
    
    if (_connectedProxy) {
        delete _connectedProxy;
        _connectedProxy = nullptr;
    }
}

} // namespace ams::mitm::ldn::ryuldn
