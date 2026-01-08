#include "ryu_ldn_protocol.hpp"
#include "../debug.hpp"
#include <cstring>
#include <stdexcept>

namespace ams::mitm::ldn::ryuldn {

    RyuLdnProtocol::RyuLdnProtocol() : _bufferEnd(0) {
        std::memset(_buffer, 0, sizeof(_buffer));
    }

    RyuLdnProtocol::~RyuLdnProtocol() {
    }

    void RyuLdnProtocol::Reset() {
        _bufferEnd = 0;
    }

    void RyuLdnProtocol::Read(const u8* data, int offset, int size) {
        int index = 0;

        while (index < size) {
            if (_bufferEnd < HeaderSize) {
                // Assemble the header first
                int copyable = std::min(size - index, HeaderSize - _bufferEnd);
                std::memcpy(_buffer + _bufferEnd, data + offset + index, copyable);

                index += copyable;
                _bufferEnd += copyable;
            }

            if (_bufferEnd >= HeaderSize) {
                // The header is available. Make sure we received all the data
                LdnHeader ldnHeader;
                std::memcpy(&ldnHeader, _buffer, HeaderSize);

                if (ldnHeader.magic != RyuLdnMagic) {
                    LogFormat("RyuLDN: Invalid magic number in received packet: 0x%08x", ldnHeader.magic);
                    Reset();
                    return;
                }

                if (ldnHeader.version != ProtocolVersion) {
                    LogFormat("RyuLDN: Protocol version mismatch. Expected %d, got %d", ProtocolVersion, ldnHeader.version);
                    Reset();
                    return;
                }

                int finalSize = HeaderSize + ldnHeader.dataSize;

                if (finalSize >= MaxPacketSize) {
                    LogFormat("RyuLDN: Max packet size %d exceeded: %d", MaxPacketSize, finalSize);
                    Reset();
                    return;
                }

                int copyable = std::min(size - index, finalSize - _bufferEnd);
                std::memcpy(_buffer + _bufferEnd, data + offset + index, copyable);

                index += copyable;
                _bufferEnd += copyable;

                if (finalSize == _bufferEnd) {
                    // The full packet has been retrieved. Decode it
                    const u8* ldnData = _buffer + HeaderSize;

                    DecodeAndHandle(ldnHeader, ldnData);

                    Reset();
                }
            }
        }
    }

    void RyuLdnProtocol::DecodeAndHandle(const LdnHeader& header, const u8* data) {
        PacketId packetId = static_cast<PacketId>(header.type);

        switch (packetId) {
            case PacketId::Initialize: {
                if (onInitialize) {
                    InitializeMessage msg;
                    ParseStruct(data, msg);
                    onInitialize(header, msg);
                }
                break;
            }

            case PacketId::Passphrase: {
                if (onPassphrase) {
                    PassphraseMessage msg;
                    ParseStruct(data, msg);
                    onPassphrase(header, msg);
                }
                break;
            }

            case PacketId::Connected: {
                if (onConnected) {
                    NetworkInfo info;
                    ParseStruct(data, info);
                    onConnected(header, info);
                }
                break;
            }

            case PacketId::SyncNetwork: {
                if (onSyncNetwork) {
                    NetworkInfo info;
                    ParseStruct(data, info);
                    onSyncNetwork(header, info);
                }
                break;
            }

            case PacketId::ScanReply: {
                if (onScanReply) {
                    NetworkInfo info;
                    ParseStruct(data, info);
                    onScanReply(header, info);
                }
                break;
            }

            case PacketId::ScanReplyEnd: {
                if (onScanReplyEnd) {
                    onScanReplyEnd(header);
                }
                break;
            }

            case PacketId::Disconnect: {
                if (onDisconnected) {
                    DisconnectMessage msg;
                    ParseStruct(data, msg);
                    onDisconnected(header, msg);
                }
                break;
            }

            case PacketId::RejectReply: {
                if (onRejectReply) {
                    onRejectReply(header);
                }
                break;
            }

            case PacketId::ProxyConfig: {
                if (onProxyConfig) {
                    ProxyConfig config;
                    ParseStruct(data, config);
                    onProxyConfig(header, config);
                }
                break;
            }

            case PacketId::ExternalProxy: {
                if (onExternalProxy) {
                    ExternalProxyConfig config;
                    ParseStruct(data, config);
                    onExternalProxy(header, config);
                }
                break;
            }

            case PacketId::ExternalProxyToken: {
                if (onExternalProxyToken) {
                    ExternalProxyToken token;
                    ParseStruct(data, token);
                    onExternalProxyToken(header, token);
                }
                break;
            }

            case PacketId::ExternalProxyState: {
                if (onExternalProxyState) {
                    ExternalProxyConnectionState state;
                    ParseStruct(data, state);
                    onExternalProxyState(header, state);
                }
                break;
            }

            case PacketId::Ping: {
                if (onPing) {
                    PingMessage msg;
                    ParseStruct(data, msg);
                    onPing(header, msg);
                }
                break;
            }

            case PacketId::NetworkError: {
                if (onNetworkError) {
                    NetworkErrorMessage msg;
                    ParseStruct(data, msg);
                    onNetworkError(header, msg);
                }
                break;
            }

            case PacketId::ProxyConnect: {
                if (onProxyConnect) {
                    ProxyConnectRequestFull msg;
                    ParseStruct(data, msg);
                    onProxyConnect(header, msg);
                }
                break;
            }

            case PacketId::ProxyConnectReply: {
                if (onProxyConnectReply) {
                    ProxyConnectResponseFull msg;
                    ParseStruct(data, msg);
                    onProxyConnectReply(header, msg);
                }
                break;
            }

            case PacketId::ProxyData: {
                if (onProxyData) {
                    ProxyDataHeaderFull hdr;
                    ParseStruct(data, hdr);
                    const u8* payload = data + sizeof(ProxyDataHeaderFull);
                    u32 payloadSize = hdr.dataLength;
                    onProxyData(header, hdr, payload, payloadSize);
                }
                break;
            }

            case PacketId::ProxyDisconnect: {
                if (onProxyDisconnect) {
                    ProxyDisconnectMessageFull msg;
                    ParseStruct(data, msg);
                    onProxyDisconnect(header, msg);
                }
                break;
            }

            default:
                LogFormat("RyuLDN: Unhandled packet type: %d", static_cast<int>(packetId));
                break;
        }
    }

    void RyuLdnProtocol::EncodeHeader(PacketId type, int dataSize, u8* output) {
        LdnHeader header;
        header.magic = RyuLdnMagic;
        header.version = ProtocolVersion;
        header.type = static_cast<u8>(type);
        header.dataSize = dataSize;

        std::memcpy(output, &header, HeaderSize);
    }

    int RyuLdnProtocol::Encode(PacketId type, u8* output) {
        LdnHeader header;
        header.magic = RyuLdnMagic;
        header.version = ProtocolVersion;
        header.type = static_cast<u8>(type);
        header.dataSize = 0;

        std::memcpy(output, &header, HeaderSize);

        return HeaderSize;
    }

    int RyuLdnProtocol::Encode(PacketId type, const u8* data, int dataSize, u8* output) {
        LdnHeader header;
        header.magic = RyuLdnMagic;
        header.version = ProtocolVersion;
        header.type = static_cast<u8>(type);
        header.dataSize = dataSize;

        std::memcpy(output, &header, HeaderSize);
        std::memcpy(output + HeaderSize, data, dataSize);

        return HeaderSize + dataSize;
    }
}
