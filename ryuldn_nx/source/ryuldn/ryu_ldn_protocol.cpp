#include "ryu_ldn_protocol.hpp"
#include "../debug.hpp"

namespace ams::mitm::ldn::ryuldn {

    void RyuLdnProtocol::Reset() {
        if (_currentBuffer && _pool) {
            _pool->ReturnBuffer(_currentBuffer);
            _currentBuffer = nullptr;
        }
        _headerBytesReceived = 0;
        _bufferEnd = 0;
        _inPacket = false;
    }

    void RyuLdnProtocol::Read(const u8* data, int offset, int size) {
        // Thread-safe read operation (defensive programming)
        std::lock_guard<os::Mutex> lock(_readMutex);
        
        int index = 0;

        while (index < size) {
            // Phase 1: Assemble header (16 bytes)
            if (_headerBytesReceived < HeaderSize) {
                int copyable = std::min(size - index, HeaderSize - _headerBytesReceived);
                std::memcpy(_headerBuffer + _headerBytesReceived, data + index + offset, copyable);
                
                index += copyable;
                _headerBytesReceived += copyable;
                
                // Continue to next iteration to check if header is complete
                continue;
            }

            // Phase 2: Header complete - validate and borrow buffer if needed
            if (_headerBytesReceived >= HeaderSize && !_inPacket) {
                LdnHeader header;
                std::memcpy(&header, _headerBuffer, HeaderSize);

                if (header.magic != RyuLdnMagic) {
                    LOG_INFO_ARGS(COMP_RLDN_PROTOCOL, "RyuLdnProtocol: Invalid magic 0x%08x (expected 0x%08x)", 
                             header.magic, RyuLdnMagic);
                    Reset();
                    return;
                }

                if (header.version != ProtocolVersion) {
                    LOG_INFO_ARGS(COMP_RLDN_PROTOCOL, "RyuLdnProtocol: Invalid version %u (expected %u)", 
                             header.version, ProtocolVersion);
                    Reset();
                    return;
                }

                if (header.dataSize >= MaxPacketSize - HeaderSize) {
                    LOG_INFO_ARGS(COMP_RLDN_PROTOCOL, "RyuLdnProtocol: Packet too large (%d bytes)", header.dataSize);
                    Reset();
                    return;
                }

                // Borrow buffer for packet data
                _currentBuffer = _pool->BorrowBuffer(TimeSpan::FromSeconds(5));
                if (!_currentBuffer) {
                    LOG_INFO(COMP_RLDN_PROTOCOL, "RyuLdnProtocol: Failed to borrow buffer - dropping packet");
                    // Skip this packet's data
                    int skipBytes = std::min(size - index, header.dataSize);
                    index += skipBytes;
                    _headerBytesReceived = 0;  // Reset to receive next header
                    continue;
                }

                _inPacket = true;
                _bufferEnd = 0;
            }

            // Phase 3: Receive packet data
            if (_inPacket && _currentBuffer) {
                LdnHeader header;
                std::memcpy(&header, _headerBuffer, HeaderSize);
                
                int finalSize = header.dataSize;
                int copyable = std::min(size - index, finalSize - _bufferEnd);

                std::memcpy(_currentBuffer + _bufferEnd, data + index + offset, copyable);

                index += copyable;
                _bufferEnd += copyable;

                // Phase 4: Packet complete - decode and handle
                if (_bufferEnd >= finalSize) {
                    DecodeAndHandle(header, _currentBuffer);
                    
                    // Return buffer immediately
                    _pool->ReturnBuffer(_currentBuffer);
                    _currentBuffer = nullptr;
                    
                    // Reset for next packet
                    _headerBytesReceived = 0;
                    _bufferEnd = 0;
                    _inPacket = false;
                }
            }
        }
    }

    void RyuLdnProtocol::DecodeAndHandle(const LdnHeader& header, const u8* data) {
        switch (static_cast<PacketId>(header.type)) {
            case PacketId::Initialize:
                if (onInitialize) {
                    InitializeMessage msg;
                    ParseStruct(data, msg);
                    onInitialize(header, msg);
                }
                break;

            case PacketId::Passphrase:
                if (onPassphrase) {
                    PassphraseMessage msg;
                    ParseStruct(data, msg);
                    onPassphrase(header, msg);
                }
                break;

            case PacketId::Connected:
                if (onConnected) {
                    NetworkInfo info;
                    ParseStruct(data, info);
                    onConnected(header, info);
                }
                break;

            case PacketId::SyncNetwork:
                if (onSyncNetwork) {
                    NetworkInfo info;
                    ParseStruct(data, info);
                    onSyncNetwork(header, info);
                }
                break;

            case PacketId::ScanReply:
                if (onScanReply) {
                    NetworkInfo info;
                    ParseStruct(data, info);
                    onScanReply(header, info);
                }
                break;

            case PacketId::ScanReplyEnd:
                if (onScanReplyEnd) {
                    onScanReplyEnd(header);
                }
                break;

            case PacketId::Disconnect:
                if (onDisconnected) {
                    DisconnectMessage msg;
                    ParseStruct(data, msg);
                    onDisconnected(header, msg);
                }
                break;

            case PacketId::RejectReply:
                if (onRejectReply) {
                    onRejectReply(header);
                }
                break;

            case PacketId::Reject:
                if (onReject) {
                    RejectRequest req;
                    ParseStruct(data, req);
                    onReject(header, req);
                }
                break;

            case PacketId::CreateAccessPoint:
                if (onCreateAccessPoint) {
                    CreateAccessPointRequest req;
                    ParseStruct(data, req);
                    u32 extraSize = header.dataSize - sizeof(req);
                    onCreateAccessPoint(header, req, data + sizeof(req), extraSize);
                }
                break;

            case PacketId::CreateAccessPointPrivate:
                if (onCreateAccessPointPrivate) {
                    CreateAccessPointPrivateRequest req;
                    ParseStruct(data, req);
                    u32 extraSize = header.dataSize - sizeof(req);
                    onCreateAccessPointPrivate(header, req, data + sizeof(req), extraSize);
                }
                break;

            case PacketId::SetAcceptPolicy:
                if (onSetAcceptPolicy) {
                    SetAcceptPolicyRequest req;
                    ParseStruct(data, req);
                    onSetAcceptPolicy(header, req);
                }
                break;

            case PacketId::SetAdvertiseData:
                if (onSetAdvertiseData) {
                    onSetAdvertiseData(header, data, header.dataSize);
                }
                break;

            case PacketId::Connect:
                if (onConnect) {
                    ConnectRequest req;
                    ParseStruct(data, req);
                    onConnect(header, req);
                }
                break;

            case PacketId::ConnectPrivate:
                if (onConnectPrivate) {
                    ConnectPrivateRequest req;
                    ParseStruct(data, req);
                    onConnectPrivate(header, req);
                }
                break;

            case PacketId::Scan:
                if (onScan) {
                    ScanFilter filter;
                    ParseStruct(data, filter);
                    onScan(header, filter);
                }
                break;

            case PacketId::ProxyConfig:
                if (onProxyConfig) {
                    ProxyConfig config;
                    ParseStruct(data, config);
                    onProxyConfig(header, config);
                }
                break;

            case PacketId::ExternalProxy:
                if (onExternalProxy) {
                    ExternalProxyConfig config;
                    ParseStruct(data, config);
                    onExternalProxy(header, config);
                }
                break;

            case PacketId::ExternalProxyToken:
                if (onExternalProxyToken) {
                    ExternalProxyToken token;
                    ParseStruct(data, token);
                    onExternalProxyToken(header, token);
                }
                break;

            case PacketId::ExternalProxyState:
                if (onExternalProxyState) {
                    ExternalProxyConnectionState state;
                    ParseStruct(data, state);
                    onExternalProxyState(header, state);
                }
                break;

            case PacketId::ProxyConnect:
                if (onProxyConnect) {
                    ProxyConnectRequestFull req;
                    ParseStruct(data, req);
                    onProxyConnect(header, req);
                }
                break;

            case PacketId::ProxyConnectReply:
                if (onProxyConnectReply) {
                    ProxyConnectResponseFull resp;
                    ParseStruct(data, resp);
                    onProxyConnectReply(header, resp);
                }
                break;

            case PacketId::ProxyData:
                if (onProxyData) {
                    ProxyDataHeaderFull hdr;
                    ParseStruct(data, hdr);
                    u32 payloadSize = header.dataSize - sizeof(hdr);
                    onProxyData(header, hdr, data + sizeof(hdr), payloadSize);
                }
                break;

            case PacketId::ProxyDisconnect:
                if (onProxyDisconnect) {
                    ProxyDisconnectMessageFull msg;
                    ParseStruct(data, msg);
                    onProxyDisconnect(header, msg);
                }
                break;

            case PacketId::Ping:
                if (onPing) {
                    PingMessage msg;
                    ParseStruct(data, msg);
                    onPing(header, msg);
                }
                break;

            case PacketId::NetworkError:
                if (onNetworkError) {
                    NetworkErrorMessage msg;
                    ParseStruct(data, msg);
                    onNetworkError(header, msg);
                }
                break;

            default:
                LOG_INFO_ARGS(COMP_RLDN_PROTOCOL, "RyuLdnProtocol: Unknown packet type %u", header.type);
                break;
        }
    }

    void RyuLdnProtocol::EncodeHeader(PacketId type, int dataSize, u8* output) {
        LdnHeader header;
        header.magic = RyuLdnMagic;
        header.type = static_cast<u8>(type);
        header.version = ProtocolVersion;
        header.dataSize = dataSize;
        std::memcpy(output, &header, HeaderSize);
    }

    int RyuLdnProtocol::Encode(PacketId type, u8* output) {
        EncodeHeader(type, 0, output);
        return HeaderSize;
    }

    int RyuLdnProtocol::Encode(PacketId type, const u8* data, int dataSize, u8* output) {
        EncodeHeader(type, dataSize, output);
        std::memcpy(output + HeaderSize, data, dataSize);
        return HeaderSize + dataSize;
    }

} // namespace ams::mitm::ldn::ryuldn
