#include "ldn_proxy.hpp"
#include "ldn_proxy_socket.hpp"
#include "../ldn_master_proxy_client.hpp"
#include "../ryu_ldn_protocol.hpp"
#include "../../debug.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <algorithm>

namespace ams::mitm::ldn::ryuldn::proxy {

    LdnProxy::LdnProxy(const ProxyConfig& config, LdnMasterProxyClient* client, RyuLdnProtocol* protocol)
        : _parent(client),
          _protocol(protocol),
          _socketsMutex(false),
          _packetBufferMutex(false),
          _subnetMask(config.proxySubnetMask),
          _localIp(config.proxyIp),
          _broadcast(_localIp | (~_subnetMask))
    {
        LOG_HEAP(COMP_RLDN_PROXY,"LdnProxy constructor start");
        // Initialize ephemeral port pools for each protocol (using piecewise_construct)
        // Initialize ephemeral port pools for each protocol (using make_unique)
        _ephemeralPorts[IPPROTO_UDP] = std::make_unique<EphemeralPortPool>();
        _ephemeralPorts[IPPROTO_TCP] = std::make_unique<EphemeralPortPool>();

        // Allocate shared packet buffer
        _packetBuffer = std::make_unique<u8[]>(MaxPacketSize);

        RegisterHandlers(protocol);

        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy created: IP=0x%08x, Mask=0x%08x, Broadcast=0x%08x", _localIp, _subnetMask, _broadcast);
        LOG_HEAP(COMP_RLDN_PROXY,"LdnProxy constructor end");
    }

    LdnProxy::~LdnProxy() {
        Dispose();
    }

    void LdnProxy::RegisterHandlers(RyuLdnProtocol* protocol) {
        // Register callbacks with protocol handler
        protocol->onProxyConnect = [this](const LdnHeader& header, const ProxyConnectRequestFull& request) {
            HandleConnectionRequest(header, request);
        };

        protocol->onProxyConnectReply = [this](const LdnHeader& header, const ProxyConnectResponseFull& response) {
            HandleConnectionResponse(header, response);
        };

        protocol->onProxyData = [this](const LdnHeader& header, const ProxyDataHeaderFull& proxyHeader, const u8* data, u32 dataSize) {
            HandleData(header, proxyHeader, data, dataSize);
        };

        protocol->onProxyDisconnect = [this](const LdnHeader& header, const ProxyDisconnectMessageFull& disconnect) {
            HandleDisconnect(header, disconnect);
        };

        _protocol = protocol;
    }

    void LdnProxy::UnregisterHandlers(RyuLdnProtocol* protocol) {
        protocol->onProxyConnect = nullptr;
        protocol->onProxyConnectReply = nullptr;
        protocol->onProxyData = nullptr;
        protocol->onProxyDisconnect = nullptr;
    }

    bool LdnProxy::Supported(s32 domain, [[maybe_unused]] s32 type, s32 protocol) {
        if (protocol == IPPROTO_TCP) {
            LOG_INFO(COMP_RLDN_PROXY,"LdnProxy: TCP proxy networking is untested");
        }

        return domain == AF_INET && (protocol == IPPROTO_TCP || protocol == IPPROTO_UDP);
    }

    u16 LdnProxy::GetEphemeralPort(s32 protocolType) {
        auto it = _ephemeralPorts.find(protocolType);
        if (it != _ephemeralPorts.end()) {
            return it->second->AllocatePort();
        }
        return 49152; // Fallback
    }

    void LdnProxy::ReturnEphemeralPort(s32 protocolType, u16 port) {
        auto it = _ephemeralPorts.find(protocolType);
        if (it != _ephemeralPorts.end()) {
            it->second->ReturnPort(port);
        }
    }

    void LdnProxy::RegisterSocket(LdnProxySocket* socket) {
        std::scoped_lock lk(_socketsMutex);
        _sockets.push_back(socket);
        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy: Socket registered (total: %zu)", _sockets.size());
    }

    void LdnProxy::UnregisterSocket(LdnProxySocket* socket) {
        std::scoped_lock lk(_socketsMutex);
        _sockets.remove(socket);
        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy: Socket unregistered (total: %zu)", _sockets.size());
    }

    void LdnProxy::ForRoutedSockets(const ProxyInfo& info, std::function<void(LdnProxySocket*)> action) {
        std::scoped_lock lk(_socketsMutex);

        for (auto* socket : _sockets) {
            // Must match protocol and destination port
            if (socket->GetProtocolType() != static_cast<s32>(info.protocol)) {
                continue;
            }

            const sockaddr_in& endpoint = socket->GetLocalEndPoint();
            if (ntohs(endpoint.sin_port) != info.destPort) {
                continue;
            }

            // We can assume packets routed to us have been sent to our destination
            // They will either be sent to us, or broadcast packets
            action(socket);
        }
    }

    u32 LdnProxy::GetIpV4(const sockaddr_in* endpoint) {
        if (endpoint == nullptr) {
            return 0;
        }

        if (endpoint->sin_family != AF_INET) {
            return 0; // Not supported
        }

        return ntohl(endpoint->sin_addr.s_addr);
    }

    ProxyInfo LdnProxy::MakeInfo(const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType) {
        ProxyInfo info;
        info.sourceIpV4 = GetIpV4(localEp);
        info.sourcePort = ntohs(localEp->sin_port);
        info.destIpV4 = GetIpV4(remoteEp);
        info.destPort = ntohs(remoteEp->sin_port);
        info.protocol = protocolType;
        return info;
    }

    void LdnProxy::HandleConnectionRequest([[maybe_unused]] const LdnHeader& header, const ProxyConnectRequestFull& request) {
        ForRoutedSockets(request.info, [&request](LdnProxySocket* socket) {
            socket->IncomingConnectionRequest(request);
        });
    }

    void LdnProxy::HandleConnectionResponse([[maybe_unused]] const LdnHeader& header, const ProxyConnectResponseFull& response) {
        ForRoutedSockets(response.info, [&response](LdnProxySocket* socket) {
            socket->HandleConnectResponse(response);
        });
    }

    void LdnProxy::HandleData([[maybe_unused]] const LdnHeader& header, const ProxyDataHeaderFull& proxyHeader, const u8* data, u32 dataSize) {
        ProxyDataPacket packet;
        packet.header = proxyHeader;
        packet.data.assign(data, data + dataSize);

        ForRoutedSockets(proxyHeader.info, [&packet](LdnProxySocket* socket) {
            socket->IncomingData(packet);
        });
    }

    void LdnProxy::HandleDisconnect([[maybe_unused]] const LdnHeader& header, const ProxyDisconnectMessageFull& disconnect) {
        ForRoutedSockets(disconnect.info, [&disconnect](LdnProxySocket* socket) {
            socket->HandleDisconnect(disconnect);
        });
    }

    void LdnProxy::RequestConnection(const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType) {
        // We must ask the other side to initialize a connection, so they can accept a socket for us
        ProxyConnectRequestFull request;
        request.info = MakeInfo(localEp, remoteEp, protocolType);

        std::scoped_lock lock(_packetBufferMutex);
        u8* packet = _packetBuffer.get();
        int packetSize = RyuLdnProtocol::Encode(PacketId::ProxyConnect, request, packet);

        _parent->SendRawPacket(packet, packetSize);

        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy: RequestConnection from %08x:%u to %08x:%u (proto %d)",
                 request.info.sourceIpV4, request.info.sourcePort,
                 request.info.destIpV4, request.info.destPort,
                 protocolType);
    }

    void LdnProxy::SignalConnected(const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType) {
        // We must tell the other side that we have accepted their request for connection
        ProxyConnectResponseFull response;
        response.info = MakeInfo(localEp, remoteEp, protocolType);

        std::scoped_lock lock(_packetBufferMutex);
        u8* packet = _packetBuffer.get();
        int packetSize = RyuLdnProtocol::Encode(PacketId::ProxyConnectReply, response, packet);

        _parent->SendRawPacket(packet, packetSize);

        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy: SignalConnected from %08x:%u to %08x:%u",
                 response.info.sourceIpV4, response.info.sourcePort,
                 response.info.destIpV4, response.info.destPort);
    }

    void LdnProxy::EndConnection(const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType) {
        // We must tell the other side that our connection is dropped
        ProxyDisconnectMessageFull message;
        message.info = MakeInfo(localEp, remoteEp, protocolType);
        message.reason = DisconnectReason::None; // TODO: proper disconnect reason

        std::scoped_lock lock(_packetBufferMutex);
        u8* packet = _packetBuffer.get();
        int packetSize = RyuLdnProtocol::Encode(PacketId::ProxyDisconnect, message, packet);

        _parent->SendRawPacket(packet, packetSize);

        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy: EndConnection from %08x:%u to %08x:%u",
                 message.info.sourceIpV4, message.info.sourcePort,
                 message.info.destIpV4, message.info.destPort);
    }

    s32 LdnProxy::SendTo(const u8* buffer, size_t bufferSize, [[maybe_unused]] s32 flags, const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType) {
        // We send exactly as much as the user wants us to, currently instantly
        // TODO: handle over "virtual mtu" (we have a max packet size to worry about anyways)
        // fragment if tcp? throw if udp?

        ProxyDataHeaderFull header;
        header.info = MakeInfo(localEp, remoteEp, protocolType);
        header.dataLength = bufferSize;

        std::scoped_lock lock(_packetBufferMutex);
        u8* packet = _packetBuffer.get();
        int packetSize = RyuLdnProtocol::Encode(PacketId::ProxyData, header, buffer, bufferSize, packet);

        _parent->SendRawPacket(packet, packetSize);

        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy: SendTo %zu bytes from %08x:%u to %08x:%u",
                 bufferSize,
                 header.info.sourceIpV4, header.info.sourcePort,
                 header.info.destIpV4, header.info.destPort);

        return bufferSize;
    }

    // BSD socket compatibility methods (temporary - for bsd_mitm_service)
    s32 LdnProxy::SendTo(s32 fd, const u8* buffer, size_t bufferSize, const sockaddr_in* dest) {
        // For now, just log and return success
        // TODO: Implement proper socket tracking with file descriptors
        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy::SendTo(fd=%d): Compatibility method called, size=%zu", fd, bufferSize);

        // Create dummy local endpoint
        sockaddr_in localEp;
        std::memset(&localEp, 0, sizeof(localEp));
        localEp.sin_family = AF_INET;
        localEp.sin_addr.s_addr = htonl(_localIp);
        localEp.sin_port = 0; // Will be filled by socket layer

        return SendTo(buffer, bufferSize, 0, &localEp, dest, IPPROTO_UDP);
    }

    Result LdnProxy::RecvFrom(s32 fd, [[maybe_unused]] u8* buffer, [[maybe_unused]] size_t bufferSize, size_t* received, [[maybe_unused]] sockaddr_in* from) {
        // For now, just log and return failure (would block)
        // TODO: Implement proper socket tracking with file descriptors
        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy::RecvFrom(fd=%d): Compatibility method called", fd);
        if (received) {
            *received = 0;
        }
        return MAKERESULT(Module_Libnx, LibnxError_IoError); // Would block
    }

    void LdnProxy::CleanupSocket(s32 fd) {
        // For now, just log
        // TODO: Implement proper socket tracking with file descriptors
        LOG_INFO_ARGS(COMP_RLDN_PROXY,"LdnProxy::CleanupSocket(fd=%d): Compatibility method called", fd);
    }

    void LdnProxy::Dispose() {
        if (_protocol) {
            UnregisterHandlers(_protocol);
        }

        std::scoped_lock lk(_socketsMutex);
        // Note: Sockets should close themselves, we don't call ProxyDestroyed()
        // as it doesn't exist in our C++ implementation
        _sockets.clear();

        LOG_INFO(COMP_RLDN_PROXY,"LdnProxy: Disposed");
    }

} // namespace ams::mitm::ldn::ryuldn::proxy
