#pragma once
// LDN Proxy Socket - Virtual Socket Implementation
// Matches Ryujinx LdnRyu/Proxy/LdnProxySocket.cs
// This socket is forwarded through a TCP stream that goes through the Ldn server.
// The Ldn server will then route the packets we send (or need to receive) within the virtual adhoc network.

#include "../types.hpp"
#include <stratosphere.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>

namespace ams::mitm::ldn::ryuldn::proxy {

    // Forward declarations
    class LdnProxy;

    // WSA Error codes (for BSD socket compatibility)
    enum class WsaError : s32 {
        WSAEWOULDBLOCK = 10035,
        WSAECONNREFUSED = 10061,
        WSAEINVAL = 10022,
        WSAEOPNOTSUPP = 10045,
        WSAEISCONN = 10056,
        WSAENOTCONN = 10057,
        WSAESHUTDOWN = 10058,
        WSAEMSGSIZE = 10040,
    };

    // Proxy data packet (wrapper for ProxyDataHeader + data)
    struct ProxyDataPacket {
        ProxyDataHeaderFull header;
        std::vector<u8> data;
    };

    // Socket option names (subset of BSD socket options)
    enum class SocketOptionName : s32 {
        Broadcast = 0x20,
        DontLinger = 0x80,
        Debug = 0x1,
        Error = 0x1007,
        KeepAlive = 0x8,
        OutOfBandInline = 0x100,
        ReceiveBuffer = 0x1002,
        ReceiveTimeout = 0x1006,
        SendBuffer = 0x1001,
        SendTimeout = 0x1005,
        Type = 0x1008,
        ReuseAddress = 0x4,
    };

    // LDN Proxy Socket Implementation
    class LdnProxySocket {
    private:
        LdnProxy* _proxy;

        bool _isListening;
        std::vector<LdnProxySocket*> _listenSockets;
        os::Mutex _listenSocketsMutex;

        std::queue<ProxyConnectRequestFull> _connectRequests;
        mutable os::Mutex _connectRequestsMutex;

        os::SystemEvent _acceptEvent;
        s32 _acceptTimeout;

        std::queue<s32> _errors;
        mutable os::Mutex _errorsMutex;

        os::SystemEvent _connectEvent;
        ProxyConnectResponseFull _connectResponse;

        s32 _receiveTimeout;
        os::SystemEvent _receiveEvent;
        std::queue<ProxyDataPacket> _receiveQueue;
        mutable os::Mutex _receiveQueueMutex;

        bool _connecting;
        bool _broadcast;
        bool _readShutdown;
        bool _writeShutdown;
        bool _closed;

        std::unordered_map<SocketOptionName, s32> _socketOptions;

        sockaddr_in _remoteEndPoint;
        sockaddr_in _localEndPoint;

        bool _connected;
        bool _isBound;

        s32 _addressFamily;  // AF_INET
        s32 _socketType;     // SOCK_STREAM, SOCK_DGRAM
        s32 _protocolType;   // IPPROTO_TCP, IPPROTO_UDP

        bool _blocking;

        // Helper methods
        sockaddr_in EnsureLocalEndpoint(bool replace);
        sockaddr_in GetEndpoint(u32 ipv4, u16 port);
        void SignalError(WsaError error);

    public:
        LdnProxySocket(s32 addressFamily, s32 socketType, s32 protocolType, LdnProxy* proxy);
        ~LdnProxySocket();

        // Socket operations
        void Accept(LdnProxySocket** out_socket);
        void Bind(const sockaddr_in* localEP);
        void Close();
        void Connect(const sockaddr_in* remoteEP);
        void Disconnect(bool reuseSocket);
        void Listen(s32 backlog);

        s32 Receive(u8* buffer, size_t bufferSize, s32 flags);
        s32 ReceiveFrom(u8* buffer, size_t bufferSize, s32 flags, sockaddr_in* outSrcAddr);
        s32 Send(const u8* buffer, size_t bufferSize, s32 flags);
        s32 SendTo(const u8* buffer, size_t bufferSize, s32 flags, const sockaddr_in* destAddr);

        void Shutdown(s32 how);

        // Socket options
        s32 GetSocketOption(SocketOptionName optionName);
        void SetSocketOption(SocketOptionName optionName, s32 optionValue);

        // Packet handling (called by LdnProxy)
        void IncomingData(const ProxyDataPacket& packet);
        void IncomingConnectionRequest(const ProxyConnectRequestFull& request);
        void HandleConnectResponse(const ProxyConnectResponseFull& response);
        void HandleDisconnect(const ProxyDisconnectMessageFull& msg);

        // Properties
        bool IsConnected() const { return _connected; }
        bool IsBound() const { return _isBound; }
        bool IsListening() const { return _isListening; }
        bool IsBlocking() const { return _blocking; }
        void SetBlocking(bool blocking) { _blocking = blocking; }

        const sockaddr_in& GetRemoteEndPoint() const { return _remoteEndPoint; }
        const sockaddr_in& GetLocalEndPoint() const { return _localEndPoint; }

        s32 GetAddressFamily() const { return _addressFamily; }
        s32 GetSocketType() const { return _socketType; }
        s32 GetProtocolType() const { return _protocolType; }

        // Query state
        s32 GetAvailable() const;
        bool IsReadable() const;
        bool IsWritable() const;
        bool HasError() const;

        // Internal helper for accepted sockets
        LdnProxySocket* AsAccepted(const sockaddr_in& remoteEp);
    };

} // namespace ams::mitm::ldn::ryuldn::proxy
