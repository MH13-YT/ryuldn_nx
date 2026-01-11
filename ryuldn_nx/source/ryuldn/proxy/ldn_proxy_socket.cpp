#include "ldn_proxy_socket.hpp"
#include "ldn_proxy.hpp"
#include "../../debug.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <algorithm>

namespace ams::mitm::ldn::ryuldn::proxy {

    LdnProxySocket::LdnProxySocket(s32 addressFamily, s32 socketType, s32 protocolType, LdnProxy* proxy)
        : _proxy(proxy),
          _isListening(false),
          _listenSocketsMutex(false),
          _connectRequestsMutex(false),
          _acceptEvent(os::EventClearMode_AutoClear, false),
          _acceptTimeout(-1),
          _errorsMutex(false),
          _connectEvent(os::EventClearMode_AutoClear, false),
          _receiveTimeout(-1),
          _receiveEvent(os::EventClearMode_AutoClear, false),
          _receiveQueueMutex(false),
          _connecting(false),
          _broadcast(false),
          _readShutdown(false),
          _writeShutdown(false),
          _closed(false),
          _connected(false),
          _isBound(false),
          _addressFamily(addressFamily),
          _socketType(socketType),
          _protocolType(protocolType),
          _blocking(true)
    {
        LOG_HEAP(COMP_RLDN_PROXY_SOC,"LdnProxySocket constructor start");
        std::memset(&_remoteEndPoint, 0, sizeof(_remoteEndPoint));
        std::memset(&_localEndPoint, 0, sizeof(_localEndPoint));
        std::memset(&_connectResponse, 0, sizeof(_connectResponse));

        // Initialize socket options with defaults
        _socketOptions[SocketOptionName::Broadcast] = 0;
        _socketOptions[SocketOptionName::DontLinger] = 0;
        _socketOptions[SocketOptionName::Debug] = 0;
        _socketOptions[SocketOptionName::Error] = 0;
        _socketOptions[SocketOptionName::KeepAlive] = 0;
        _socketOptions[SocketOptionName::OutOfBandInline] = 0;
        _socketOptions[SocketOptionName::ReceiveBuffer] = 131072;
        _socketOptions[SocketOptionName::ReceiveTimeout] = -1;
        _socketOptions[SocketOptionName::SendBuffer] = 131072;
        _socketOptions[SocketOptionName::SendTimeout] = -1;
        _socketOptions[SocketOptionName::Type] = socketType;
        _socketOptions[SocketOptionName::ReuseAddress] = 0;

        // Register with proxy
        _proxy->RegisterSocket(this);

        LOG_INFO_ARGS(COMP_RLDN_PROXY_SOC,"LdnProxySocket created: family=%d, type=%d, proto=%d", addressFamily, socketType, protocolType);
        LOG_HEAP(COMP_RLDN_PROXY_SOC,"LdnProxySocket constructor end");
    }

    LdnProxySocket::~LdnProxySocket() {
        Close();
    }

    sockaddr_in LdnProxySocket::EnsureLocalEndpoint(bool replace) {
        if (_isBound && _localEndPoint.sin_port != 0) {
            if (replace) {
                _proxy->ReturnEphemeralPort(_protocolType, ntohs(_localEndPoint.sin_port));
            } else {
                return _localEndPoint;
            }
        }

        sockaddr_in localEp;
        std::memset(&localEp, 0, sizeof(localEp));
        localEp.sin_family = AF_INET;
        localEp.sin_addr.s_addr = htonl(_proxy->GetLocalIP());
        localEp.sin_port = htons(_proxy->GetEphemeralPort(_protocolType));

        _localEndPoint = localEp;
        return localEp;
    }

    sockaddr_in LdnProxySocket::GetEndpoint(u32 ipv4, u16 port) {
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(ipv4);
        addr.sin_port = htons(port);
        return addr;
    }

    void LdnProxySocket::SignalError(WsaError error) {
        std::scoped_lock lk(_errorsMutex);
        _errors.push(static_cast<s32>(error));
    }

    LdnProxySocket* LdnProxySocket::AsAccepted(const sockaddr_in& remoteEp) {
        _connected = true;
        _remoteEndPoint = remoteEp;

        sockaddr_in localEp = EnsureLocalEndpoint(true);

        _proxy->SignalConnected(&localEp, &remoteEp, _protocolType);

        return this;
    }

    void LdnProxySocket::IncomingData(const ProxyDataPacket& packet) {
        bool isBroadcast = _proxy->IsBroadcast(packet.header.info.destIpV4);

        if (!_closed && (_broadcast || !isBroadcast)) {
            std::scoped_lock lk(_receiveQueueMutex);
            _receiveQueue.push(packet);
            _receiveEvent.Signal();
        }
    }

    void LdnProxySocket::IncomingConnectionRequest(const ProxyConnectRequestFull& request) {
        std::scoped_lock lk(_connectRequestsMutex);
        _connectRequests.push(request);
        _acceptEvent.Signal();
    }

    void LdnProxySocket::HandleConnectResponse(const ProxyConnectResponseFull& response) {
        if (!_connecting) {
            return;
        }

        _connecting = false;
        _connectResponse = response;

        if (response.info.sourceIpV4 != 0) {
            _remoteEndPoint = GetEndpoint(response.info.sourceIpV4, response.info.sourcePort);
            _connected = true;
        } else {
            // Connection failed
            SignalError(WsaError::WSAECONNREFUSED);
        }

        _connectEvent.Signal();
    }

    void LdnProxySocket::HandleDisconnect([[maybe_unused]] const ProxyDisconnectMessageFull& msg) {
        Disconnect(false);
    }

    void LdnProxySocket::Accept(LdnProxySocket** out_socket) {
        if (!_isListening) {
            *out_socket = nullptr;
            return; // Error: not listening
        }

        {
            std::scoped_lock lk(_connectRequestsMutex);
            if (!_blocking && _connectRequests.empty()) {
                *out_socket = nullptr;
                return; // WSAEWOULDBLOCK
            }
        }

        while (true) {
            _acceptEvent.TimedWait(TimeSpan::FromMilliSeconds(_acceptTimeout < 0 ? -1 : _acceptTimeout));

            std::scoped_lock lk(_connectRequestsMutex);
            while (!_connectRequests.empty()) {
                ProxyConnectRequestFull request = _connectRequests.front();
                _connectRequests.pop();

                if (!_connectRequests.empty()) {
                    _acceptEvent.Signal(); // Still more accepts to do
                }

                // Is this request made for us?
                sockaddr_in endpoint = GetEndpoint(request.info.destIpV4, request.info.destPort);

                if (endpoint.sin_addr.s_addr == _localEndPoint.sin_addr.s_addr &&
                    endpoint.sin_port == _localEndPoint.sin_port) {
                    // Yes - let's accept
                    sockaddr_in remoteEndpoint = GetEndpoint(request.info.sourceIpV4, request.info.sourcePort);

                    LOG_HEAP(COMP_RLDN_PROXY_SOC,"before LdnProxySocket");
                    LdnProxySocket* socket = new (std::nothrow) LdnProxySocket(_addressFamily, _socketType, _protocolType, _proxy);
                    if (socket == nullptr) {
                        LOG_INFO(COMP_RLDN_PROXY_SOC,"ERROR: Failed to allocate LdnProxySocket - out of memory");
                        LOG_HEAP(COMP_RLDN_PROXY_SOC,"after LdnProxySocket FAILED");
                        *out_socket = nullptr;
                        return;
                    }
                    LOG_HEAP(COMP_RLDN_PROXY_SOC,"after LdnProxySocket");

                    socket->AsAccepted(remoteEndpoint);

                    {
                        std::scoped_lock listen_lk(_listenSocketsMutex);
                        _listenSockets.push_back(socket);
                    }

                    *out_socket = socket;
                    return;
                }
            }
        }
    }

    void LdnProxySocket::Bind(const sockaddr_in* localEP) {
        if (localEP == nullptr) {
            return; // Error
        }

        if (_isBound && _localEndPoint.sin_port != 0) {
            _proxy->ReturnEphemeralPort(_protocolType, ntohs(_localEndPoint.sin_port));
        }

        sockaddr_in asIPEndpoint = *localEP;
        if (asIPEndpoint.sin_port == 0) {
            asIPEndpoint.sin_port = htons(_proxy->GetEphemeralPort(_protocolType));
        }

        _localEndPoint = asIPEndpoint;
        _isBound = true;

        LOG_INFO_ARGS(COMP_RLDN_PROXY_SOC,"LdnProxySocket::Bind - port %u", ntohs(_localEndPoint.sin_port));
    }

    void LdnProxySocket::Close() {
        if (_closed) {
            return;
        }

        _closed = true;

        _proxy->UnregisterSocket(this);

        if (_connected) {
            Disconnect(false);
        }

        {
            std::scoped_lock lk(_listenSocketsMutex);
            for (auto* socket : _listenSockets) {
                socket->Close();
            }
            _listenSockets.clear();
        }

        _isListening = false;

        LOG_INFO(COMP_RLDN_PROXY_SOC,"LdnProxySocket::Close");
    }

    void LdnProxySocket::Connect(const sockaddr_in* remoteEP) {
        if (_isListening || !_isBound) {
            return; // Error: invalid operation
        }

        if (remoteEP == nullptr) {
            return; // Error
        }

        sockaddr_in localEp = EnsureLocalEndpoint(true);

        _connecting = true;

        _proxy->RequestConnection(&localEp, remoteEP, _protocolType);

        if (!_blocking && _protocolType == IPPROTO_TCP) {
            return; // WSAEWOULDBLOCK
        }

        _connectEvent.Wait();

        if (_connectResponse.info.sourceIpV4 == 0) {
            // Connection refused
            return; // Error
        }

        _connectResponse = {}; // Reset
        LOG_INFO(COMP_RLDN_PROXY_SOC,"LdnProxySocket::Connect - connected");
    }

    void LdnProxySocket::Disconnect([[maybe_unused]] bool reuseSocket) {
        if (_connected) {
            _proxy->EndConnection(&_localEndPoint, &_remoteEndPoint, _protocolType);

            std::memset(&_remoteEndPoint, 0, sizeof(_remoteEndPoint));
            _connected = false;
        }

        LOG_INFO(COMP_RLDN_PROXY_SOC,"LdnProxySocket::Disconnect");
    }

    void LdnProxySocket::Listen(s32 backlog) {
        if (!_isBound) {
            return; // Error
        }

        _isListening = true;

        LOG_INFO_ARGS(COMP_RLDN_PROXY_SOC,"LdnProxySocket::Listen - backlog %d", backlog);
    }

    s32 LdnProxySocket::Receive(u8* buffer, size_t bufferSize, s32 flags) {
        sockaddr_in dummy;
        return ReceiveFrom(buffer, bufferSize, flags, &dummy);
    }

    s32 LdnProxySocket::ReceiveFrom(u8* buffer, size_t bufferSize, s32 flags, sockaddr_in* outSrcAddr) {
        if (!_connected && _protocolType == IPPROTO_TCP) {
            return -1; // WSAECONNRESET
        }

        {
            std::scoped_lock lk(_receiveQueueMutex);
            if (!_receiveQueue.empty()) {
                ProxyDataPacket& packet = _receiveQueue.front();
                *outSrcAddr = GetEndpoint(packet.header.info.sourceIpV4, packet.header.info.sourcePort);

                bool peek = (flags & MSG_PEEK) != 0;
                size_t read;

                if (packet.data.size() > bufferSize) {
                    read = bufferSize;
                    std::memcpy(buffer, packet.data.data(), bufferSize);

                    if (_protocolType == IPPROTO_UDP) {
                        // UDP overflows, loses the data
                        if (!peek) {
                            _receiveQueue.pop();
                        }
                        return -1; // WSAEMSGSIZE
                    } else if (_protocolType == IPPROTO_TCP) {
                        // TCP splits data
                        std::vector<u8> newData(packet.data.begin() + bufferSize, packet.data.end());
                        packet.data = std::move(newData);
                    }
                } else {
                    read = packet.data.size();
                    std::memcpy(buffer, packet.data.data(), read);

                    if (!peek) {
                        _receiveQueue.pop();
                    }
                }

                return read;
            } else if (_readShutdown) {
                return 0;
            } else if (!_blocking) {
                return -1; // WSAEWOULDBLOCK
            }
        }

        s32 timeout = _receiveTimeout;
        if (timeout == 0) timeout = -1;

        _receiveEvent.TimedWait(TimeSpan::FromMilliSeconds(timeout));

        if (!_connected && _protocolType == IPPROTO_TCP) {
            return -1; // WSAECONNRESET
        }

        std::scoped_lock lk(_receiveQueueMutex);
        if (!_receiveQueue.empty()) {
            ProxyDataPacket& packet = _receiveQueue.front();
            *outSrcAddr = GetEndpoint(packet.header.info.sourceIpV4, packet.header.info.sourcePort);

            size_t read = std::min(bufferSize, packet.data.size());
            std::memcpy(buffer, packet.data.data(), read);

            if ((flags & MSG_PEEK) == 0) {
                _receiveQueue.pop();
            }

            return read;
        } else if (_readShutdown) {
            return 0;
        } else {
            return -1; // WSAETIMEDOUT
        }
    }

    s32 LdnProxySocket::Send(const u8* buffer, size_t bufferSize, s32 flags) {
        if (!_connected) {
            return -1; // Error
        }

        return SendTo(buffer, bufferSize, flags, &_remoteEndPoint);
    }

    s32 LdnProxySocket::SendTo(const u8* buffer, size_t bufferSize, s32 flags, const sockaddr_in* destAddr) {
        if (!_connected && _protocolType == IPPROTO_TCP) {
            return -1; // WSAECONNRESET
        }

        sockaddr_in localEp = EnsureLocalEndpoint(false);

        if (destAddr == nullptr) {
            return -1; // Error
        }

        return _proxy->SendTo(buffer, bufferSize, flags, &localEp, destAddr, _protocolType);
    }

    void LdnProxySocket::Shutdown(s32 how) {
        if (how == SHUT_RD || how == SHUT_RDWR) {
            _readShutdown = true;
            _receiveEvent.Signal();
        }
        if (how == SHUT_WR || how == SHUT_RDWR) {
            _writeShutdown = true;
        }

        LOG_INFO_ARGS(COMP_RLDN_PROXY_SOC,"LdnProxySocket::Shutdown - how %d", how);
    }

    s32 LdnProxySocket::GetSocketOption(SocketOptionName optionName) {
        auto it = _socketOptions.find(optionName);
        if (it != _socketOptions.end()) {
            return it->second;
        }
        return 0;
    }

    void LdnProxySocket::SetSocketOption(SocketOptionName optionName, s32 optionValue) {
        _socketOptions[optionName] = optionValue;

        if (optionName == SocketOptionName::ReceiveTimeout) {
            _receiveTimeout = optionValue;
        } else if (optionName == SocketOptionName::Broadcast) {
            _broadcast = (optionValue != 0);
        }

        LOG_INFO_ARGS(COMP_RLDN_PROXY_SOC,"LdnProxySocket::SetSocketOption - %d = %d", static_cast<s32>(optionName), optionValue);
    }

    s32 LdnProxySocket::GetAvailable() const {
        std::scoped_lock lk(_receiveQueueMutex);
        s32 result = 0;
        std::queue<ProxyDataPacket> tempQueue = _receiveQueue;
        while (!tempQueue.empty()) {
            result += tempQueue.front().data.size();
            tempQueue.pop();
        }
        return result;
    }

    bool LdnProxySocket::IsReadable() const {
        if (_isListening) {
            std::scoped_lock lk(_connectRequestsMutex);
            return !_connectRequests.empty();
        } else {
            if (_readShutdown) {
                return true;
            }
            std::scoped_lock lk(_receiveQueueMutex);
            return !_receiveQueue.empty();
        }
    }

    bool LdnProxySocket::IsWritable() const {
        return _connected || _protocolType == IPPROTO_UDP;
    }

    bool LdnProxySocket::HasError() const {
        std::scoped_lock lk(_errorsMutex);
        return !_errors.empty();
    }

} // namespace ams::mitm::ldn::ryuldn::proxy
