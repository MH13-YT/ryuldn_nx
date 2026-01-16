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
        
        LOG_DBG_ARGS(COMP_RLDN_PROTOCOL," Read: Processing %d bytes (offset=%d)", size, offset);
        
        int index = 0;

        while (index < size) {
            // Phase 1: Assemble header (10 bytes - sizeof(LdnHeader))
            if (_headerBytesReceived < HeaderSize) {
                int copyable = std::min(size - index, HeaderSize - _headerBytesReceived);
                std::memcpy(_headerBuffer + _headerBytesReceived, data + index + offset, copyable);
                
                index += copyable;
                _headerBytesReceived += copyable;
                
                // If header is not yet complete, continue to next iteration
                if (_headerBytesReceived < HeaderSize) {
                    continue;
                }
                // Header is complete, fall through to Phase 2 to process it
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

                // Special case: dataSize=0 means no payload, handle immediately
                if (header.dataSize == 0) {
                    LOG_DBG_ARGS(COMP_RLDN_PROTOCOL,"  Packet complete (no payload) at index=%d, calling DecodeAndHandle", index);
                    DecodeAndHandle(header, nullptr);
                    _headerBytesReceived = 0;
                    LOG_DBG_ARGS(COMP_RLDN_PROTOCOL,"  Reset state (no payload), continuing to index=%d (size=%d)", index, size);
                    continue;
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
                    LOG_DBG_ARGS(COMP_RLDN_PROTOCOL,"  Packet complete at index=%d, calling DecodeAndHandle", index);
                    DecodeAndHandle(header, _currentBuffer);
                    
                    // Return buffer immediately
                    _pool->ReturnBuffer(_currentBuffer);
                    _currentBuffer = nullptr;
                    
                    // Reset for next packet
                    _headerBytesReceived = 0;
                    _bufferEnd = 0;
                    _inPacket = false;
                    LOG_DBG_ARGS(COMP_RLDN_PROTOCOL,"  Reset state, continuing to index=%d (size=%d)", index, size);
                }
            }
        }
        LOG_DBG_ARGS(COMP_RLDN_PROTOCOL," Read: Finished processing, index=%d size=%d", index, size);
    }

    void RyuLdnProtocol::DecodeAndHandle(const LdnHeader& header, const u8* data) {
        LOG_DBG_ARGS(COMP_RLDN_PROTOCOL," DecodeAndHandle: PacketId=%d, dataSize=%u", header.type, header.dataSize);
        
        switch (static_cast<PacketId>(header.type)) {
            case PacketId::Initialize:
                LOG_DBG(COMP_RLDN_PROTOCOL,"  -> Handling Initialize");
                if (onInitialize) {
                    InitializeMessage msg;
                    ParseStruct(data, msg);
                    onInitialize(header, msg);
                }
                break;

            case PacketId::Passphrase:
                LOG_DBG(COMP_RLDN_PROTOCOL,"  -> Handling Passphrase");
                if (onPassphrase) {
                    PassphraseMessage msg;
                    ParseStruct(data, msg);
                    onPassphrase(header, msg);
                }
                break;

            case PacketId::Connected:
                LOG_DBG(COMP_RLDN_PROTOCOL,"  -> Handling Connected");
                if (onConnected) {
                    LdnNetworkInfo ldnInfo;
                    ParseNetworkInfo(data, header.dataSize, ldnInfo);
                    // Note: We receive full NetworkInfo but extract only LdnNetworkInfo
                    // Convert to NetworkInfo for callback compatibility
                    NetworkInfo info;
                    std::memset(&info, 0, sizeof(info));
                    info.ldn = ldnInfo;
                    onConnected(header, info);
                }
                break;

            case PacketId::SyncNetwork:
                LOG_DBG(COMP_RLDN_PROTOCOL,"  -> Handling SyncNetwork");
                if (onSyncNetwork) {
                    LdnNetworkInfo ldnInfo;
                    ParseNetworkInfo(data, header.dataSize, ldnInfo);
                    NetworkInfo info;
                    std::memset(&info, 0, sizeof(info));
                    info.ldn = ldnInfo;
                    onSyncNetwork(header, info);
                }
                break;

            case PacketId::ScanReply:
                LOG_DBG(COMP_RLDN_PROTOCOL,"  -> Handling ScanReply");
                if (onScanReply) {
                    LdnNetworkInfo ldnInfo;
                    ParseNetworkInfo(data, header.dataSize, ldnInfo);
                    NetworkInfo info;
                    std::memset(&info, 0, sizeof(info));
                    info.ldn = ldnInfo;
                    onScanReply(header, info);
                }
                break;

            case PacketId::ScanReplyEnd:
                LOG_INFO(COMP_RLDN_PROTOCOL," -> Handling ScanReplyEnd - should signal scanEvent");
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
