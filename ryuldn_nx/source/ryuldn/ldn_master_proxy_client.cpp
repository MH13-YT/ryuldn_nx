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
      _workerThread{},  // Zero-initialize thread structure
      _protocol(g_sharedBufferPool)  // Use shared BufferPool
{

    _socket = -1;
    _connected = false;
    _networkConnected = false;
    _stop = false;
    _hostedProxy = nullptr;
    _connectedProxy = nullptr;
    _disconnectReason = DisconnectReason::None;
    _disconnectIp = 0;
    _lastError = NetworkError::None;

    // Allocate thread stack only
    _threadStack.reset(new (std::nothrow) u8[ThreadStackSize + os::ThreadStackAlignment]);
    
    if (!_threadStack) {
        AMS_ABORT("LdnMasterProxyClient: Failed to allocate thread stack");
    }

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
    _protocol.onSetAdvertiseData = [this](const LdnHeader& h, const u8* d, u32 s) { HandleSetAdvertiseData(h, d, s); };
    _protocol.onSetAcceptPolicy = [this](const LdnHeader& h, const SetAcceptPolicyRequest& r) { HandleSetAcceptPolicy(h, r); };
    _protocol.onCreateAccessPoint = [this](const LdnHeader& h, const CreateAccessPointRequest& r, const u8* d, u32 s) { HandleCreateAccessPoint(h, r, d, s); };
    _protocol.onCreateAccessPointPrivate = [this](const LdnHeader& h, const CreateAccessPointPrivateRequest& r, const u8* d, u32 s) { HandleCreateAccessPointPrivate(h, r, d, s); };
    _protocol.onConnect = [this](const LdnHeader& h, const ConnectRequest& r) { HandleConnect(h, r); };
    _protocol.onConnectPrivate = [this](const LdnHeader& h, const ConnectPrivateRequest& r) { HandleConnectPrivate(h, r); };
    _protocol.onScan = [this](const LdnHeader& h, const ScanFilter& f) { HandleScanRequest(h, f); };
    _protocol.onReject = [this](const LdnHeader& h, const RejectRequest& r) { HandleReject(h, r); };
}

LdnMasterProxyClient::~LdnMasterProxyClient() {
    static_cast<void>(Finalize());

    // SystemEventContainer in _events is automatically destroyed
    // No manual cleanup needed for events

    if (_hostedProxy) delete _hostedProxy;
    if (_connectedProxy) delete _connectedProxy;
}

Result LdnMasterProxyClient::Initialize() {
    if (!_threadStack) return MAKERESULT(0xFD, 1);
    
    // Initialize timeout handler with explicit nothrow allocation
    _timeout.reset(new (std::nothrow) NetworkTimeout(InactiveTimeout, [this]() {
        this->TimeoutConnection();
    }));
    
    if (!_timeout) {
        AMS_ABORT("LdnMasterProxyClient: Failed to allocate NetworkTimeout");
    }
    
    uintptr_t stackAddr = reinterpret_cast<uintptr_t>(_threadStack.get());
    uintptr_t alignedAddr = util::AlignUp(stackAddr, os::ThreadStackAlignment);
    void* stackTop = reinterpret_cast<void*>(alignedAddr);
    
    // Create worker thread - reinitialize to ensure clean state
    std::memset(&_workerThread, 0, sizeof(_workerThread));
    LOG_INFO(COMP_RLDN_CLI, "[THREAD-DIAG] === Creating LDN-MASTER WORKER THREAD ===");
    LOG_INFO(COMP_RLDN_CLI, "[THREAD-DIAG]   Thread type: WORKER (LDN Master client)");
    LOG_INFO_ARGS(COMP_RLDN_CLI, "[THREAD-DIAG]   Thread struct @ %p", &_workerThread);
    LOG_INFO_ARGS(COMP_RLDN_CLI, "[THREAD-DIAG]   Stack buffer @ %p", _threadStack.get());
    LOG_INFO_ARGS(COMP_RLDN_CLI, "[THREAD-DIAG]   Stack top (aligned) @ %p", stackTop);
    LOG_INFO_ARGS(COMP_RLDN_CLI, "[THREAD-DIAG]   Stack size: 0x%x (%d bytes)", ThreadStackSize, ThreadStackSize);
    LOG_INFO_ARGS(COMP_RLDN_CLI, "[THREAD-DIAG]   Priority: %d (0x%x)", 0x15, 0x15);
    LOG_INFO_ARGS(COMP_RLDN_CLI, "[THREAD-DIAG]   Ideal core: %d", 2);
    LOG_INFO_ARGS(COMP_RLDN_CLI, "[THREAD-DIAG]   Alignment check: 0x%lx & 0xF = 0x%lx", (uintptr_t)stackTop, (uintptr_t)stackTop & 0xF);
    Result rc = os::CreateThread(&_workerThread, WorkerThreadFunc, this, stackTop, ThreadStackSize, 0x15, 2);
    LOG_INFO_ARGS(COMP_RLDN_CLI, "[THREAD-DIAG] >>> CreateThread returned: 0x%x (%s)", rc.GetValue(), R_SUCCEEDED(rc) ? "SUCCESS" : "FAILED");
    if (R_FAILED(rc)) {
        LOG_INFO(COMP_RLDN_CLI, "[THREAD-DIAG] !!! WORKER THREAD CREATION FAILED !!!");
        LOG_INFO_ARGS(COMP_RLDN_CLI, "[THREAD-DIAG]   Error code: 0x%x", rc.GetValue());
        return rc;
    }
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
    std::memset(&_workerThread, 0, sizeof(_workerThread));
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
            _events.errorEvent.Signal();
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
    LOG_DBG_ARGS(COMP_RLDN_MASTER," EnsureConnected: Attempting connection to %s:%d", _serverAddress.c_str(), _serverPort);
    _events.errorEvent.Clear();
    _events.connectedEvent.Clear();
    _socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_socket < 0) {
        LOG_ERR_ARGS(COMP_RLDN_MASTER,"EnsureConnected: socket() failed, errno=%d", errno);
        return false;
    }
    LOG_DBG_ARGS(COMP_RLDN_MASTER," EnsureConnected: Socket created (fd=%d)", _socket);
    int flags = fcntl(_socket, F_GETFL, 0);
    fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
    struct addrinfo hints, *result;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", _serverPort);
    LOG_DBG_ARGS(COMP_RLDN_MASTER," EnsureConnected: Calling getaddrinfo(%s, %s)", _serverAddress.c_str(), portStr);
    if (getaddrinfo(_serverAddress.c_str(), portStr, &hints, &result) != 0) {
        LOG_ERR_ARGS(COMP_RLDN_MASTER,"EnsureConnected: getaddrinfo() failed, errno=%d", errno);
        close(_socket); _socket = -1; return false;
    }
    LOG_DBG(COMP_RLDN_MASTER," EnsureConnected: DNS resolved, attempting connect");
    int rc = ::connect(_socket, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    if (rc < 0 && errno != EINPROGRESS) {
        LOG_ERR_ARGS(COMP_RLDN_MASTER,"EnsureConnected: connect() failed, errno=%d", errno);
        close(_socket); _socket = -1; return false;
    }
    LOG_DBG_ARGS(COMP_RLDN_MASTER," EnsureConnected: Connection in progress, polling (timeout=%dms)", FailureTimeout);
    struct pollfd pfd;
    pfd.fd = _socket; pfd.events = POLLOUT;
    int poll_result = poll(&pfd, 1, FailureTimeout);
    if (poll_result <= 0) {
        LOG_ERR_ARGS(COMP_RLDN_MASTER,"EnsureConnected: poll() %s, result=%d, errno=%d",
                  poll_result == 0 ? "timed out" : "failed", poll_result, errno);
        close(_socket); _socket = -1; return false;
    }
    LOG_DBG(COMP_RLDN_MASTER," EnsureConnected: Poll succeeded, checking socket error");
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(_socket, SOL_SOCKET, SO_ERROR, &error, &len);
    if (error != 0) {
        LOG_ERR_ARGS(COMP_RLDN_MASTER,"EnsureConnected: Socket error after connect: %d", error);
        close(_socket); _socket = -1; return false;
    }
    LOG_INFO(COMP_RLDN_MASTER,"EnsureConnected: Successfully connected to server!");

    // Switch socket back to blocking mode for reliable data transmission
    flags = fcntl(_socket, F_GETFL, 0);
    fcntl(_socket, F_SETFL, flags & ~O_NONBLOCK);
    LOG_DBG(COMP_RLDN_MASTER," Switched socket to blocking mode");

    // NOTE: Keep Nagle enabled to encourage coalescing of small packets; disabling it caused header/data splits.
    LOG_DBG(COMP_RLDN_MASTER," TCP_NODELAY left disabled to favor packet coalescing");

    _connected = true;
    _protocol.Reset();
    _events.connectedEvent.Signal();

    // Give the server a moment to be ready
    os::SleepThread(TimeSpan::FromMilliSeconds(100));

    // Borrow buffer from pool for sending
    ScopedBuffer buffer(g_sharedBufferPool);
    if (!buffer.Get()) {
        LOG_ERR(COMP_RLDN_MASTER,"EnsureConnected: Failed to borrow buffer for Initialize packet");
        _connected = false;
        return false;
    }

    // Debug: Log _initializeMemory content
    LOG_DBG(COMP_RLDN_MASTER," _initializeMemory content:");
    LOG_DBG_ARGS(COMP_RLDN_MASTER,"   ID: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
              _initializeMemory.id[0], _initializeMemory.id[1], _initializeMemory.id[2], _initializeMemory.id[3],
              _initializeMemory.id[4], _initializeMemory.id[5], _initializeMemory.id[6], _initializeMemory.id[7],
              _initializeMemory.id[8], _initializeMemory.id[9], _initializeMemory.id[10], _initializeMemory.id[11],
              _initializeMemory.id[12], _initializeMemory.id[13], _initializeMemory.id[14], _initializeMemory.id[15]);
    LOG_DBG_ARGS(COMP_RLDN_MASTER,"   MAC: %02X:%02X:%02X:%02X:%02X:%02X",
              _initializeMemory.macAddress[0], _initializeMemory.macAddress[1], _initializeMemory.macAddress[2],
              _initializeMemory.macAddress[3], _initializeMemory.macAddress[4], _initializeMemory.macAddress[5]);

    // Debug: Verify structure sizes
    LOG_DBG_ARGS(COMP_RLDN_MASTER," sizeof(LdnHeader)=%zu, sizeof(InitializeMessage)=%zu",
              sizeof(LdnHeader), sizeof(InitializeMessage));

    int size = RyuLdnProtocol::Encode(PacketId::Initialize, _initializeMemory, buffer.Get());

    // Debug: Log the packet being sent
    LOG_DBG_ARGS(COMP_RLDN_MASTER," Sending Initialize packet: size=%d bytes", size);
    LOG_DBG(COMP_RLDN_MASTER," Full packet dump (first 32 bytes):");
    for (int i = 0; i < 32 && i < size; i += 16) {
        int remaining = size - i;
        if (remaining >= 16) {
            LOG_DBG_ARGS(COMP_RLDN_MASTER,"   [%02d-%02d]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                      i, i+15,
                      buffer.Get()[i+0], buffer.Get()[i+1], buffer.Get()[i+2], buffer.Get()[i+3],
                      buffer.Get()[i+4], buffer.Get()[i+5], buffer.Get()[i+6], buffer.Get()[i+7],
                      buffer.Get()[i+8], buffer.Get()[i+9], buffer.Get()[i+10], buffer.Get()[i+11],
                      buffer.Get()[i+12], buffer.Get()[i+13], buffer.Get()[i+14], buffer.Get()[i+15]);
        }
    }

    int sent = SendPacket(buffer.Get(), size);
    LOG_DBG_ARGS(COMP_RLDN_MASTER," SendPacket returned: %d (expected %d)", sent, size);
    if (sent != size) {
        LOG_ERR(COMP_RLDN_MASTER,"Failed to send complete packet!");
        _connected = false;
        return false;
    }

    // Wait for Initialize response from server (processed by WorkerLoop thread)
    LOG_DBG(COMP_RLDN_MASTER," Waiting for Initialize response from server...");
    LOG_DBG(COMP_RLDN_MASTER," [EVENT-TRACE] Clearing initializeEvent before wait");
    _events.initializeEvent.Clear();
    LOG_DBG(COMP_RLDN_MASTER," [EVENT-TRACE] Starting TimedWait (5000ms) on initializeEvent");
    
    if (!_events.initializeEvent.TimedWait(TimeSpan::FromMilliSeconds(5000))) {
        LOG_ERR(COMP_RLDN_MASTER,"[EVENT-TRACE] TimedWait TIMEOUT after 5000 ms - event never signaled");
        _connected = false;
        return false;
    }
    
    LOG_INFO(COMP_RLDN_MASTER,"[EVENT-TRACE] TimedWait SUCCESS - initializeEvent was signaled");
    LOG_INFO(COMP_RLDN_MASTER,"Successfully initialized connection with server");
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

        if (_networkChangeCallback) {
            NetworkInfo info{};
            _networkChangeCallback(info, false, _disconnectReason);
        }
        
        // Refresh timeout when disconnecting from network (Ryujinx behavior)
        if (_timeout) {
            _timeout->RefreshTimeout();
        }
    }
}

int LdnMasterProxyClient::SendPacket(const u8* data, int size) {
    if (!_connected || _socket < 0) {
        LOG_ERR_ARGS(COMP_RLDN_MASTER,"SendPacket: Not connected (_connected=%d, _socket=%d)", _connected, _socket);
        return -1;
    }
    LOG_DBG_ARGS(COMP_RLDN_MASTER," SendPacket: Sending %d bytes", size);
    
    // Hex dump header + up to 64 bytes (always logs the header bytes even if packet is larger)
    {
        int dump_len = std::min(size, 64);
        char hexdump[512] = {0};
        int hexpos = 0;
        for (int i = 0; i < dump_len && hexpos < (int)sizeof(hexdump) - 4; i++) {
            hexpos += snprintf(hexdump + hexpos, sizeof(hexdump) - hexpos, "%02X ", data[i]);
        }
        LOG_DBG_ARGS(COMP_RLDN_MASTER," SendPacket hex dump first %d bytes (total %d): %s", dump_len, size, hexdump);
    }
    
    std::lock_guard<std::mutex> lock(_sendMutex);
    int sent = 0;
    while (sent < size) {
        int rc = ::send(_socket, data + sent, size - sent, 0);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_DBG_ARGS(COMP_RLDN_MASTER," SendPacket: EAGAIN/EWOULDBLOCK, retrying... (sent %d/%d)", sent, size);
                os::SleepThread(TimeSpan::FromMilliSeconds(1));
                continue;
            }
            LOG_ERR_ARGS(COMP_RLDN_MASTER,"SendPacket: send() failed, errno=%d (sent %d/%d)", errno, sent, size);
            return -1;
        }
        sent += rc;
        LOG_DBG_ARGS(COMP_RLDN_MASTER," SendPacket: Sent %d bytes (total %d/%d)", rc, sent, size);
    }
    LOG_DBG_ARGS(COMP_RLDN_MASTER," SendPacket: Successfully sent all %d bytes", sent);
    
    // Small delay to help TCP coalesce packets and avoid fragmentation
    // Bumped to 10ms to better group header+payload for the server's single-threaded parser
    os::SleepThread(TimeSpan::FromMilliSeconds(10));
    
    return sent;
}

int LdnMasterProxyClient::SendRawPacket(const u8* data, int size) { return SendPacket(data, size); }

int LdnMasterProxyClient::ReceiveData() {
    if (!_connected || _socket < 0) {
        LOG_ERR_ARGS(COMP_RLDN_MASTER,"ReceiveData: Not connected (_connected=%d, _socket=%d)", _connected, _socket);
        return -1;
    }
    
    // Protect socket read to prevent multiple concurrent recv() calls
    std::lock_guard<std::mutex> lock(_receiveMutex);
    
    u8 buffer[4096];
    int received = ::recv(_socket, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (received < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            LOG_DBG_ARGS(COMP_RLDN_MASTER," ReceiveData: EAGAIN/EWOULDBLOCK (errno=%d)", err);
            return 0;
        }
        LOG_ERR_ARGS(COMP_RLDN_MASTER,"ReceiveData: recv() failed, errno=%d", err);
        return -1;
    }
    if (received == 0) {
        LOG_WARN(COMP_RLDN_MASTER,"ReceiveData: Connection closed by server (received 0 bytes)");
        return -1;
    }
    LOG_DBG_ARGS(COMP_RLDN_MASTER," ReceiveData: Received %d bytes, processing protocol", received);
    
    // Hex dump for debugging
    if (received > 0 && received <= 64) {
        char hexdump[256] = {0};
        int hexpos = 0;
        for (int i = 0; i < received && hexpos < 250; i++) {
            hexpos += snprintf(hexdump + hexpos, sizeof(hexdump) - hexpos, "%02X ", buffer[i]);
        }
        LOG_DBG_ARGS(COMP_RLDN_MASTER," ReceiveData hex dump (%d bytes): %s", received, hexdump);
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
        ScopedBuffer buffer(g_sharedBufferPool);
        if (!buffer.Get()) return;
        PassphraseMessage msg{};
        strncpy(msg.passphrase, passphrase, sizeof(msg.passphrase) - 1);
        int size = RyuLdnProtocol::Encode(PacketId::Passphrase, msg, buffer.Get());
        SendPacket(buffer.Get(), size);
    }
}

void LdnMasterProxyClient::SetNetworkChangeCallback(NetworkChangeCallback cb) { _networkChangeCallback = cb; }
void LdnMasterProxyClient::SetProxyConfigCallback(ProxyConfigCallback cb) { _proxyConfigCallback = cb; }
void LdnMasterProxyClient::SetProxyDataCallback(ProxyDataCallback cb) { _proxyDataCallback = cb; }
void LdnMasterProxyClient::SetGameVersion(const u8* v, size_t s) { std::memcpy(_gameVersion, v, std::min(s, sizeof(_gameVersion))); }
void LdnMasterProxyClient::SetPassphrase(const char* p) { UpdatePassphraseIfNeeded(p); }

void LdnMasterProxyClient::HandleInitialize(const LdnHeader&, const InitializeMessage& m) {
    LOG_DBG(COMP_RLDN_MASTER," [EVENT-TRACE] HandleInitialize: Entered handler, about to signal event");
    _initializeMemory = m;
    LOG_DBG(COMP_RLDN_MASTER," [EVENT-TRACE] HandleInitialize: Calling Signal() on initializeEvent");
    _events.initializeEvent.Signal();
    LOG_DBG(COMP_RLDN_MASTER," [EVENT-TRACE] HandleInitialize: Signal() completed successfully");
}
void LdnMasterProxyClient::HandleConnected(const LdnHeader&, const NetworkInfo& i) {
    LOG_INFO(COMP_RLDN_MASTER,"HandleConnected: Network connected");
    _networkConnected = true;
    _disconnectReason = DisconnectReason::None;
    _disconnectIp = 0;
    _lastNetworkInfo = i;
    _events.apConnectedEvent.Signal();
    if (_networkChangeCallback) {
        LOG_DBG(COMP_RLDN_MASTER," HandleConnected: Calling network change callback");
        _networkChangeCallback(i, true, DisconnectReason::None);
    }
}
void LdnMasterProxyClient::HandleSyncNetwork(const LdnHeader&, const NetworkInfo& i) {
    _lastNetworkInfo = i;
    if (_networkChangeCallback) _networkChangeCallback(i, true, DisconnectReason::None);
}
void LdnMasterProxyClient::HandleDisconnected(const LdnHeader&, const DisconnectMessage& msg) { 
    LOG_WARN_ARGS(COMP_RLDN_MASTER,"HandleDisconnected: Received disconnect message, ip=0x%08x", msg.disconnectIp);
    _disconnectIp = msg.disconnectIp;
    DisconnectInternal(); 
}
void LdnMasterProxyClient::HandleRejectReply(const LdnHeader&) { _events.rejectEvent.Signal(); }
void LdnMasterProxyClient::HandleScanReply(const LdnHeader&, const NetworkInfo& i) { _availableGames.push_back(i); }
void LdnMasterProxyClient::HandleScanReplyEnd(const LdnHeader&) { _events.scanEvent.Signal(); }
void LdnMasterProxyClient::HandleProxyConfig(const LdnHeader& h, const ProxyConfig& c) { 
    _config = c; if (_proxyConfigCallback) _proxyConfigCallback(h, c); 
}
void LdnMasterProxyClient::HandleProxyData(const LdnHeader& h, const ProxyDataHeaderFull& hdr, const u8* p, u32 s) {
    if (_proxyDataCallback) _proxyDataCallback(h, hdr, p, s);
}
void LdnMasterProxyClient::HandlePing(const LdnHeader&, const PingMessage& p) {
    if (p.requester == 0) {
        ScopedBuffer buffer(g_sharedBufferPool);
        if (!buffer.Get()) return;
        int size = RyuLdnProtocol::Encode(PacketId::Ping, p, buffer.Get());
        SendPacket(buffer.Get(), size);
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

void LdnMasterProxyClient::HandleSetAdvertiseData(const LdnHeader&, const u8* data, u32 size) {
    LOG_INFO_ARGS(COMP_RLDN_MASTER,"HandleSetAdvertiseData: size=%u", size);
    AMS_UNUSED(data);
}

void LdnMasterProxyClient::HandleSetAcceptPolicy(const LdnHeader&, const SetAcceptPolicyRequest& req) {
    LOG_INFO_ARGS(COMP_RLDN_MASTER,"HandleSetAcceptPolicy: policy=%u", req.stationAcceptPolicy);
}

void LdnMasterProxyClient::HandleCreateAccessPoint(const LdnHeader&, const CreateAccessPointRequest& req, const u8* data, u32 size) {
    LOG_INFO_ARGS(COMP_RLDN_MASTER,"HandleCreateAccessPoint: advertise=%u, passphrase_size=%u", size, req.securityConfig.passphraseSize);
    AMS_UNUSED(data);
}

void LdnMasterProxyClient::HandleCreateAccessPointPrivate(const LdnHeader&, const CreateAccessPointPrivateRequest& req, const u8* data, u32 size) {
    LOG_INFO_ARGS(COMP_RLDN_MASTER,"HandleCreateAccessPointPrivate: advertise=%u, passphrase_size=%u", size, req.securityConfig.passphraseSize);
    AMS_UNUSED(data);
}

void LdnMasterProxyClient::HandleConnect(const LdnHeader&, const ConnectRequest&) {
    LOG_INFO(COMP_RLDN_MASTER,"HandleConnect: ignored on client side");
}

void LdnMasterProxyClient::HandleConnectPrivate(const LdnHeader&, const ConnectPrivateRequest&) {
    LOG_INFO(COMP_RLDN_MASTER,"HandleConnectPrivate: ignored on client side");
}

void LdnMasterProxyClient::HandleScanRequest(const LdnHeader&, const ScanFilter&) {
    LOG_INFO(COMP_RLDN_MASTER,"HandleScanRequest: ignored on client side");
}

void LdnMasterProxyClient::HandleReject(const LdnHeader&, const RejectRequest& req) {
    LOG_INFO_ARGS(COMP_RLDN_MASTER,"HandleReject: nodeId=%u", req.nodeId);
    _disconnectReason = req.disconnectReason;
}

NetworkError LdnMasterProxyClient::ConsumeNetworkError() {
    NetworkError res = _lastError; _lastError = NetworkError::None; return res;
}

Result LdnMasterProxyClient::CreateNetwork(const CreateAccessPointRequest& req, const u8* d, u16 s) {
    LOG_INFO_ARGS(COMP_RLDN_MASTER,"CreateNetwork: Starting network creation, SSID=%s, passphrase_size=%u, useP2pProxy=%d", 
              req.securityConfig.passphraseSize > 0 ? "***HIDDEN***" : "PUBLIC",
              req.securityConfig.passphraseSize,
              _useP2pProxy);
    
    if (_timeout) {
        LOG_DBG(COMP_RLDN_MASTER," CreateNetwork: Disabling timeout before creation");
        _timeout->DisableTimeout();
    }
    
    if (!EnsureConnected()) {
        LOG_ERR(COMP_RLDN_MASTER,"CreateNetwork: Failed to connect to master server");
        DisconnectProxy();
        return MAKERESULT(0xFD, 1);
    }
    
    LOG_DBG_ARGS(COMP_RLDN_MASTER," CreateNetwork: Configuring network with P2P proxy (_useP2pProxy=%d)", _useP2pProxy);
    DisconnectInternal();
    CreateAccessPointRequest mod = req;
    std::memcpy(mod.ryuNetworkConfig.gameVersion, _gameVersion, sizeof(_gameVersion));
    ConfigureAccessPoint(mod.ryuNetworkConfig);
    
    ScopedBuffer buffer(g_sharedBufferPool);
    if (!buffer.Get()) {
        LOG_ERR(COMP_RLDN_MASTER,"CreateNetwork: Failed to borrow buffer");
        return MAKERESULT(0xFD, 1);
    }
    int sz = RyuLdnProtocol::Encode(PacketId::CreateAccessPoint, mod, d, s, buffer.Get());
    SendPacket(buffer.Get(), sz);
    
    if (_events.apConnectedEvent.TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout))) {
        LOG_INFO(COMP_RLDN_MASTER,"CreateNetwork: apConnectedEvent signaled (host) -> delivering NetworkChange");
        if (mod.ryuNetworkConfig.externalProxyPort != 0) {
            LOG_INFO_ARGS(COMP_RLDN_MASTER,"CreateNetwork: P2P proxy configured on external port %u", 
                     mod.ryuNetworkConfig.externalProxyPort);
        } else {
            LOG_INFO(COMP_RLDN_MASTER,"CreateNetwork: Using master server relay (no P2P proxy)");
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
            _networkChangeCallback(dummyInfo, true, DisconnectReason::None);
            LOG_INFO(COMP_RLDN_MASTER,"CreateNetwork: Sent immediate NetworkChange event");
        }
        
        return ResultSuccess();
    }
    LOG_ERR(COMP_RLDN_MASTER,"CreateNetwork: Timeout waiting for access point creation (apConnectedEvent not signaled)");
    return MAKERESULT(0xFD, 2);
}

Result LdnMasterProxyClient::Connect(const ConnectRequest& req) {
    if (_timeout) {
        _timeout->DisableTimeout();
    }
    
    LOG_DBG(COMP_RLDN_MASTER," Connect: Attempting to connect to network");
    if (!EnsureConnected()) {
        LOG_ERR(COMP_RLDN_MASTER,"Connect: Failed to connect to master server");
        return MAKERESULT(0xFD, 1);
    }
    ScopedBuffer buffer(g_sharedBufferPool);
    if (!buffer.Get()) return MAKERESULT(0xFD, 1);
    int sz = RyuLdnProtocol::Encode(PacketId::Connect, req, buffer.Get());
    SendPacket(buffer.Get(), sz);
    if (_events.apConnectedEvent.TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout))) {
        LOG_INFO(COMP_RLDN_MASTER,"Connect: Successfully connected to network");
        return ResultSuccess();
    }
    LOG_ERR(COMP_RLDN_MASTER,"Connect: Timeout waiting for connection");
    return MAKERESULT(0xFD, 2);
}

Result LdnMasterProxyClient::Scan(NetworkInfo* nets, u16* count, const ScanFilter& f) {
    LOG_INFO(COMP_RLDN_MASTER,"Scan: Starting scan operation");
    // Reset timeout only if not connected to a network (Ryujinx behavior)
    if (!_networkConnected && _timeout) {
        LOG_DBG(COMP_RLDN_MASTER," Scan: Refreshing timeout (not network connected)");
        _timeout->RefreshTimeout();
    }

    // Clear previous results
    _availableGames.clear();
    
    if (!EnsureConnected()) { 
        LOG_ERR(COMP_RLDN_MASTER,"Scan: Failed to ensure connection to master server");
        *count = 0;
        return MAKERESULT(0xFD, 1); 
    }
    
    LOG_DBG(COMP_RLDN_MASTER," Scan: Connected, sending scan request");
    // Reset scan event and send scan request
    _events.scanEvent.Clear();
    {
        ScopedBuffer buffer(g_sharedBufferPool);
        if (!buffer.Get()) {
            LOG_ERR(COMP_RLDN_MASTER,"Scan: Failed to borrow buffer");
            *count = 0;
            return MAKERESULT(0xFD, 1);
        }
        int sz = RyuLdnProtocol::Encode(PacketId::Scan, f, buffer.Get());
        LOG_DBG_ARGS(COMP_RLDN_MASTER," Scan: Encoded packet size=%d", sz);
        LOG_DBG_ARGS(COMP_RLDN_MASTER," Scan: ScanFilter size=%zu bytes", sizeof(ScanFilter));
        
        // Log first bytes of encoded packet
        if (sz > 0) {
            char hexdump[256] = {0};
            int hexpos = 0;
            int dumpSize = (sz < 80) ? sz : 80;
            for (int i = 0; i < dumpSize && hexpos < 250; i++) {
                hexpos += snprintf(hexdump + hexpos, sizeof(hexdump) - hexpos, "%02X ", buffer.Get()[i]);
            }
            LOG_DBG_ARGS(COMP_RLDN_MASTER," Scan: Encoded packet hex (first %d bytes): %s", dumpSize, hexdump);
        }
        
        SendPacket(buffer.Get(), sz);
    }
    
    LOG_DBG_ARGS(COMP_RLDN_MASTER," Scan: Waiting for scan results (timeout=%ums)", ScanTimeout);
    // Wait for scan completion (ScanReplyEnd) with timeout
    if (!_events.scanEvent.TimedWait(TimeSpan::FromMilliSeconds(ScanTimeout))) { 
        LOG_WARN_ARGS(COMP_RLDN_MASTER,"Scan: Timeout waiting for scan results after %ums", ScanTimeout);
        *count = 0;
        return ResultSuccess();  // Timeout is not an error
    }
    
    // Copy results
    u16 found = std::min(static_cast<u16>(_availableGames.size()), *count);
    for (u16 i = 0; i < found; i++) nets[i] = _availableGames[i];
    *count = found;
    LOG_INFO_ARGS(COMP_RLDN_MASTER,"Scan: Completed, found %u networks", found);
    return ResultSuccess();
}

Result LdnMasterProxyClient::DisconnectNetwork() {
    if (_networkConnected) {
        _disconnectReason = DisconnectReason::DisconnectedByUser;
        _disconnectIp = 0;
        DisconnectMessage msg{};
        msg.disconnectIp = _disconnectIp;
        ScopedBuffer buffer(g_sharedBufferPool);
        if (buffer.Get()) {
            int sz = RyuLdnProtocol::Encode(PacketId::Disconnect, msg, buffer.Get());
            SendPacket(buffer.Get(), sz);
        }
        DisconnectInternal();
    }
    return ResultSuccess();
}

Result LdnMasterProxyClient::SetAdvertiseData(const u8* d, u16 s) {
    if (!_networkConnected) return MAKERESULT(0xFD, 3);
    ScopedBuffer buffer(g_sharedBufferPool);
    if (!buffer.Get()) return MAKERESULT(0xFD, 1);
    int sz = RyuLdnProtocol::Encode(PacketId::SetAdvertiseData, d, s, buffer.Get());
    SendPacket(buffer.Get(), sz);
    return ResultSuccess();
}

Result LdnMasterProxyClient::SetStationAcceptPolicy(u8 p) {
    if (!_networkConnected) return MAKERESULT(0xFD, 3);
    SetAcceptPolicyRequest req{p};
    ScopedBuffer buffer(g_sharedBufferPool);
    if (!buffer.Get()) return MAKERESULT(0xFD, 1);
    int sz = RyuLdnProtocol::Encode(PacketId::SetAcceptPolicy, req, buffer.Get());
    SendPacket(buffer.Get(), sz);
    return ResultSuccess();
}

Result LdnMasterProxyClient::Reject(DisconnectReason reason, u32 nodeId) {
    if (!_networkConnected) return MAKERESULT(0xFD, 3);
    _events.rejectEvent.Clear();
    RejectRequest req;
    std::memset(&req, 0, sizeof(req));
    req.disconnectReason = reason;
    req.nodeId = nodeId;
    ScopedBuffer buffer(g_sharedBufferPool);
    if (!buffer.Get()) return MAKERESULT(0xFD, 1);
    int sz = RyuLdnProtocol::Encode(PacketId::Reject, req, buffer.Get());
    SendPacket(buffer.Get(), sz);
    if (_events.rejectEvent.TimedWait(TimeSpan::FromMilliSeconds(InactiveTimeout))) return ResultSuccess();
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
    ScopedBuffer buffer(g_sharedBufferPool);
    if (!buffer.Get()) return MAKERESULT(0xFD, 1);
    int sz = RyuLdnProtocol::Encode(PacketId::CreateAccessPointPrivate, req, d, s, buffer.Get());
    SendPacket(buffer.Get(), sz);
    if (_events.apConnectedEvent.TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout))) return ResultSuccess();
    return MAKERESULT(0xFD, 2);
}

Result LdnMasterProxyClient::ConnectPrivate(const ConnectPrivateRequest& req) {
    if (_timeout) {
        _timeout->DisableTimeout();
    }
    
    if (!EnsureConnected()) return MAKERESULT(0xFD, 1);
    ScopedBuffer buffer(g_sharedBufferPool);
    if (!buffer.Get()) return MAKERESULT(0xFD, 1);
    int sz = RyuLdnProtocol::Encode(PacketId::ConnectPrivate, req, buffer.Get());
    SendPacket(buffer.Get(), sz);
    if (_events.apConnectedEvent.TimedWait(TimeSpan::FromMilliSeconds(FailureTimeout))) return ResultSuccess();
    return MAKERESULT(0xFD, 2);
}

void LdnMasterProxyClient::ConfigureAccessPoint(RyuNetworkConfig& config) {
    if (!_useP2pProxy) {
        config.externalProxyPort = 0;
        config.internalProxyPort = 0;
        LOG_DBG(COMP_RLDN_MASTER," ConfigureAccessPoint: P2P proxy disabled");
        return;
    }
    
    // Attempt to create P2P proxy server on a port range
    for (u16 i = 0; i < proxy::P2pProxyServer::PrivatePortRange; i++) {
        u16 port = proxy::P2pProxyServer::PrivatePortBase + i;
        _hostedProxy = new (std::nothrow) proxy::P2pProxyServer(this, port, &_protocol);
        
        if (!_hostedProxy) {
            LOG_INFO(COMP_RLDN_MASTER, " ConfigureAccessPoint: Failed to allocate P2P proxy server");
            return;
        }
        
        if (!_hostedProxy->Start()) {
            delete _hostedProxy;
            _hostedProxy = nullptr;
            continue;
        }
        
        LOG_DBG_ARGS(COMP_RLDN_MASTER," ConfigureAccessPoint: P2P proxy server started on port %u", port);
        
        // Successfully started proxy, attempt UPnP NAT punch
        u16 externalPort = _hostedProxy->NatPunch();
        if (externalPort != 0) {
            // UPnP successful - configure external proxy
            config.externalProxyPort = externalPort;
            config.internalProxyPort = port;
            config.addressFamily = 2;  // InterNetwork (IPv4)
            LOG_INFO_ARGS(COMP_RLDN_MASTER,"ConfigureAccessPoint: Created P2P proxy on external port %u, internal port %u", 
                     externalPort, port);
            return;
        }
        
        // UPnP failed, cleanup and fall back to server proxy
        LOG_WARN_ARGS(COMP_RLDN_MASTER,"ConfigureAccessPoint: UPnP NAT punch failed on port %u, trying next port", port);
        _hostedProxy->Stop();
        delete _hostedProxy;
        _hostedProxy = nullptr;
    }
    
    // All ports failed or no UPnP, fall back to server proxy relay
    config.externalProxyPort = 0;
    config.internalProxyPort = 0;
    LOG_WARN(COMP_RLDN_MASTER,"ConfigureAccessPoint: Could not open external P2P port, falling back to server relay");
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
