#include "bsd_mitm_service.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>

namespace ams::mitm::ldn {

    ryuldn::proxy::LdnProxy* BsdMitmService::s_proxy = nullptr;

    BsdMitmService::BsdMitmService(std::shared_ptr<::Service> &&s, const sm::MitmProcessInfo &c)
        : sf::MitmServiceImplBase(std::forward<std::shared_ptr<::Service>>(s), c),
          socket_map_mutex(false)
    {
        LogInfo("BsdMitmService created for process: pid=%" PRIu64 ", program_id=0x%016lx",
                c.process_id, c.program_id.value);
        std::memset(socket_map, 0, sizeof(socket_map));
        LogDebug("Socket map initialized (%zu entries)", MaxSockets);
    }

    BsdMitmService::~BsdMitmService() {
        LogInfo("BsdMitmService destroyed");

        // Log any remaining open sockets
        std::scoped_lock lk(socket_map_mutex);
        int virtual_count = 0, real_count = 0;
        for (size_t i = 0; i < MaxSockets; i++) {
            if (socket_map[i].type == SocketType::Virtual) {
                virtual_count++;
                LogWarning("Virtual socket still open at destruction: fd=%zu", i);
            } else if (socket_map[i].type == SocketType::Real) {
                real_count++;
            }
        }
        if (virtual_count > 0 || real_count > 0) {
            LogWarning("Sockets at destruction: %d virtual, %d real", virtual_count, real_count);
        }
    }

    bool BsdMitmService::ShouldMitm(const sm::MitmProcessInfo &client_info) {
        AMS_UNUSED(client_info);
        // Only MITM if RyuLDN proxy is active
        bool should = s_proxy != nullptr;
        LogTrace("BsdMitmService::ShouldMitm: program_id=0x%016lx -> %s",
                 client_info.program_id.value, should ? "YES" : "NO");
        return should;
    }

    void BsdMitmService::RegisterProxy(ryuldn::proxy::LdnProxy* proxy) {
        LogInfo("BsdMitmService: Registering RyuLDN proxy at %p", (void*)proxy);
        if (!proxy) {
            LogWarning("Null proxy being registered!");
        }
        s_proxy = proxy;
        LogStateChange("proxy=null", "proxy=active");
    }

    void BsdMitmService::UnregisterProxy() {
        LogInfo("BsdMitmService: Unregistering RyuLDN proxy");
        s_proxy = nullptr;
        LogStateChange("proxy=active", "proxy=null");
    }

    BsdMitmService::SocketEntry* BsdMitmService::GetSocketEntry(s32 fd) {
        if (fd < 0 || static_cast<size_t>(fd) >= MaxSockets) {
            if (fd < 0) {
                LogWarning("GetSocketEntry: Invalid fd=%d (negative)", fd);
            } else {
                LogError("GetSocketEntry: fd=%d exceeds MaxSockets=%zu", fd, MaxSockets);
            }
            return nullptr;
        }
        return &socket_map[fd];
    }

    bool BsdMitmService::IsRyuLdnVirtualIP(u32 ip) {
        if (!s_proxy) {
            LogTrace("IsRyuLdnVirtualIP: No proxy, IP 0x%08x -> NO", ip);
            return false;
        }

        // Check if IP is in the virtual subnet
        bool result = s_proxy->IsVirtualIP(ip);
        LogTrace("IsRyuLdnVirtualIP: 0x%08x -> %s", ip, result ? "YES" : "NO");
        return result;
    }

    Result BsdMitmService::Socket(sf::Out<s32> out_fd, u32 domain, u32 type, u32 protocol) {
        LogFunctionEntry();
        LogDebug("Socket request: domain=%u, type=%u, protocol=%u", domain, type, protocol);

        // Forward to real BSD service using IPC
        struct {
            u32 domain;
            u32 type;
            u32 protocol;
        } in_args = { domain, type, protocol };

        s32 real_fd = -1;
        Result rc = serviceDispatchInOut(m_forward_service.get(), 2, in_args, real_fd);

        if (R_FAILED(rc)) {
            LogError("BSD Socket creation failed: rc=0x%x", rc.GetValue());
            out_fd.SetValue(-1);
            return rc;
        }

        // Register in our map
        std::scoped_lock lk(socket_map_mutex);
        if (real_fd >= 0 && static_cast<size_t>(real_fd) < MaxSockets) {
            socket_map[real_fd].type = SocketType::Real;
            socket_map[real_fd].real_fd = real_fd;
            socket_map[real_fd].virtual_socket = nullptr;

            LogNetSocket(real_fd, domain, type, protocol);
            LogInfo("BSD Socket registered in map: fd=%d", real_fd);
        } else {
            if (real_fd < 0) {
                LogWarning("BSD Socket returned negative fd=%d", real_fd);
            } else {
                LogError("BSD Socket fd=%d exceeds MaxSockets=%zu, cannot register!", real_fd, MaxSockets);
            }
        }

        out_fd.SetValue(real_fd);
        return ResultSuccess();
    }

    Result BsdMitmService::Bind(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr) {
        LogFunctionEntry();
        LogDebug("Bind request: fd=%d, addr_size=%zu", fd, addr.GetSize());

        // Check if binding to a virtual IP
        if (addr.GetSize() >= sizeof(sockaddr_in)) {
            const sockaddr_in* sa = reinterpret_cast<const sockaddr_in*>(addr.GetPointer());

            if (sa->sin_family == AF_INET) {
                LogDebug("Bind address: family=%d, IP=0x%08x, port=%u",
                         sa->sin_family, ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));

                if (sa->sin_addr.s_addr != INADDR_ANY && IsRyuLdnVirtualIP(ntohl(sa->sin_addr.s_addr))) {
                    LogInfo("BSD Bind to VIRTUAL IP: 0x%08x:%u",
                            ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));

                    // Mark this socket as virtual
                    std::scoped_lock lk(socket_map_mutex);
                    SocketEntry* entry = GetSocketEntry(fd);
                    if (entry) {
                        entry->type = SocketType::Virtual;
                        LogNetBind(fd, ntohs(sa->sin_port));
                        LogStateChange("socket=real", "socket=virtual");
                    } else {
                        LogError("Bind: Failed to get socket entry for fd=%d", fd);
                    }

                    // Pretend bind succeeded
                    out_ret.SetValue(0);
                    out_errno.SetValue(0);
                    return ResultSuccess();
                }
            }
        } else {
            LogWarning("Bind: addr buffer size %zu < sizeof(sockaddr_in)", addr.GetSize());
        }

        // Normal bind - forward to real BSD service
        LogDebug("Forwarding Bind to real BSD service");
        struct {
            s32 fd;
            u32 addrlen;
        } in_args = { fd, static_cast<u32>(addr.GetSize()) };

        struct {
            s32 ret;
            u32 errno_val;
        } out_args = {};

        Result rc = serviceDispatchInOut(m_forward_service.get(), 13, in_args, out_args,
            .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
            .buffers = { { addr.GetPointer(), addr.GetSize() } },
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            if (out_args.ret == 0) {
                LogInfo("Bind succeeded: fd=%d, ret=%d", fd, out_args.ret);
            } else {
                LogWarning("Bind failed: fd=%d, ret=%d, errno=%u", fd, out_args.ret, out_args.errno_val);
            }
        } else {
            LogError("Bind IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Connect(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr) {
        LogFunctionEntry();
        LogDebug("Connect request: fd=%d, addr_size=%zu", fd, addr.GetSize());

        // Check if connecting to a virtual IP
        if (addr.GetSize() >= sizeof(sockaddr_in)) {
            const sockaddr_in* sa = reinterpret_cast<const sockaddr_in*>(addr.GetPointer());

            if (sa->sin_family == AF_INET) {
                LogDebug("Connect to: IP=0x%08x, port=%u",
                         ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));

                if (IsRyuLdnVirtualIP(ntohl(sa->sin_addr.s_addr))) {
                    LogInfo("BSD Connect to VIRTUAL IP: 0x%08x:%u",
                            ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));

                    // Mark this socket as virtual
                    std::scoped_lock lk(socket_map_mutex);
                    SocketEntry* entry = GetSocketEntry(fd);
                    if (entry) {
                        entry->type = SocketType::Virtual;
                        LogNetConnect(fd, "VIRTUAL", ntohs(sa->sin_port));
                        LogStateChange("socket=real", "socket=virtual");
                    } else {
                        LogError("Connect: Failed to get socket entry for fd=%d", fd);
                    }

                    // Pretend connect succeeded
                    out_ret.SetValue(0);
                    out_errno.SetValue(0);
                    return ResultSuccess();
                }
            }
        } else {
            LogWarning("Connect: addr buffer size %zu < sizeof(sockaddr_in)", addr.GetSize());
        }

        // Normal connect - forward to real BSD service
        LogDebug("Forwarding Connect to real BSD service");
        struct {
            s32 fd;
            u32 addrlen;
        } in_args = { fd, static_cast<u32>(addr.GetSize()) };

        struct {
            s32 ret;
            u32 errno_val;
        } out_args = {};

        Result rc = serviceDispatchInOut(m_forward_service.get(), 14, in_args, out_args,
            .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
            .buffers = { { addr.GetPointer(), addr.GetSize() } },
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            if (out_args.ret == 0) {
                LogInfo("Connect succeeded: fd=%d", fd);
            } else {
                LogWarning("Connect failed: fd=%d, ret=%d, errno=%u", fd, out_args.ret, out_args.errno_val);
            }
        } else {
            LogError("Connect IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Send(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags) {
        LogTrace("Send: fd=%d, size=%zu, flags=0x%x", fd, data.GetSize(), flags);

        std::scoped_lock lk(socket_map_mutex);
        SocketEntry* entry = GetSocketEntry(fd);

        if (entry && entry->type == SocketType::Virtual && s_proxy) {
            // Send() without address - this shouldn't happen for UDP sockets
            LogWarning("BSD Send on virtual socket: fd=%d, size=%zu (no dest address!)",
                       fd, data.GetSize());

            // Can't send without destination for UDP, return error
            out_ret.SetValue(-1);
            out_errno.SetValue(ENOTCONN);
            return ResultSuccess();
        }

        // Normal send - forward to real BSD service
        LogTrace("Forwarding Send to real BSD: fd=%d, size=%zu", fd, data.GetSize());
        struct {
            s32 fd;
            u32 flags;
        } in_args = { fd, flags };

        struct {
            s32 ret;
            u32 errno_val;
        } out_args = {};

        Result rc = serviceDispatchInOut(m_forward_service.get(), 10, in_args, out_args,
            .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
            .buffers = { { data.GetPointer(), data.GetSize() } },
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            if (out_args.ret >= 0) {
                LogNetSend(fd, out_args.ret);
            } else {
                LogNetError(fd, out_args.errno_val, "Send failed");
            }
        } else {
            LogError("Send IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::SendTo(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd,
                                   sf::InAutoSelectBuffer data, u32 flags, sf::InAutoSelectBuffer addr) {
        LogTrace("SendTo: fd=%d, size=%zu, flags=0x%x, addr_size=%zu",
                 fd, data.GetSize(), flags, addr.GetSize());

        // Check destination address
        if (addr.GetSize() >= sizeof(sockaddr_in)) {
            const sockaddr_in* dest = reinterpret_cast<const sockaddr_in*>(addr.GetPointer());

            if (dest->sin_family == AF_INET) {
                LogDebug("SendTo dest: IP=0x%08x, port=%u",
                         ntohl(dest->sin_addr.s_addr), ntohs(dest->sin_port));

                if (IsRyuLdnVirtualIP(ntohl(dest->sin_addr.s_addr))) {
                    LogInfo("BSD SendTo VIRTUAL IP: 0x%08x:%u, size=%zu",
                            ntohl(dest->sin_addr.s_addr), ntohs(dest->sin_port), data.GetSize());

                    if (s_proxy) {
                        // Send via RyuLDN proxy
                        Result rc = s_proxy->SendTo(fd,
                                                   reinterpret_cast<const u8*>(data.GetPointer()),
                                                   data.GetSize(),
                                                   dest);

                        if (R_SUCCEEDED(rc)) {
                            out_ret.SetValue(data.GetSize());
                            out_errno.SetValue(0);
                            LogNetSend(fd, data.GetSize());
                            return ResultSuccess();
                        } else {
                            LogError("Proxy SendTo failed: rc=0x%x", rc.GetValue());
                        }
                    } else {
                        LogError("SendTo to virtual IP but no proxy available!");
                    }

                    // Failed
                    out_ret.SetValue(-1);
                    out_errno.SetValue(EHOSTUNREACH);
                    LogNetError(fd, EHOSTUNREACH, "Virtual SendTo failed");
                    return ResultSuccess();
                }
            }
        } else if (addr.GetSize() > 0) {
            LogWarning("SendTo: addr buffer size %zu < sizeof(sockaddr_in)", addr.GetSize());
        }

        // Normal sendto - forward to real BSD service
        LogTrace("Forwarding SendTo to real BSD");
        struct {
            s32 fd;
            u32 flags;
        } in_args = { fd, flags };

        struct {
            s32 ret;
            u32 errno_val;
        } out_args = {};

        Result rc = serviceDispatchInOut(m_forward_service.get(), 11, in_args, out_args,
            .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias, SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
            .buffers = { { data.GetPointer(), data.GetSize() }, { addr.GetPointer(), addr.GetSize() } },
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            if (out_args.ret >= 0) {
                LogNetSend(fd, out_args.ret);
            } else {
                LogNetError(fd, out_args.errno_val, "SendTo failed");
            }
        } else {
            LogError("SendTo IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Recv(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags) {
        LogTrace("Recv: fd=%d, buf_size=%zu, flags=0x%x", fd, buf.GetSize(), flags);

        std::scoped_lock lk(socket_map_mutex);
        SocketEntry* entry = GetSocketEntry(fd);

        if (entry && entry->type == SocketType::Virtual && s_proxy) {
            LogTrace("Recv on virtual socket via proxy");
            // Recv() without address buffer - receive via proxy
            sockaddr_in dummy_src;
            size_t received = 0;

            Result rc = s_proxy->RecvFrom(fd,
                                         reinterpret_cast<u8*>(buf.GetPointer()),
                                         buf.GetSize(),
                                         &received,
                                         &dummy_src);

            if (R_SUCCEEDED(rc) && received > 0) {
                out_ret.SetValue(received);
                out_errno.SetValue(0);
                LogNetRecv(fd, received);
                LogInfo("BSD Recv virtual socket: fd=%d, size=%zu", fd, received);
                return ResultSuccess();
            }

            // No data available
            LogTrace("Recv virtual: no data (EWOULDBLOCK)");
            out_ret.SetValue(-1);
            out_errno.SetValue(EWOULDBLOCK);
            return ResultSuccess();
        }

        // Normal recv - forward to real BSD service
        LogTrace("Forwarding Recv to real BSD");
        struct {
            s32 fd;
            u32 flags;
        } in_args = { fd, flags };

        struct {
            s32 ret;
            u32 errno_val;
        } out_args = {};

        Result rc = serviceDispatchInOut(m_forward_service.get(), 8, in_args, out_args,
            .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
            .buffers = { { buf.GetPointer(), buf.GetSize() } },
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            if (out_args.ret >= 0) {
                LogNetRecv(fd, out_args.ret);
            } else if (out_args.errno_val != EWOULDBLOCK && out_args.errno_val != EAGAIN) {
                LogNetError(fd, out_args.errno_val, "Recv failed");
            }
        } else {
            LogError("Recv IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::RecvFrom(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_addrlen,
                                     s32 fd, sf::OutAutoSelectBuffer buf, u32 flags, sf::OutAutoSelectBuffer addr) {
        LogTrace("RecvFrom: fd=%d, buf_size=%zu, flags=0x%x, addr_size=%zu",
                 fd, buf.GetSize(), flags, addr.GetSize());

        std::scoped_lock lk(socket_map_mutex);
        SocketEntry* entry = GetSocketEntry(fd);

        if (entry && entry->type == SocketType::Virtual && s_proxy) {
            LogTrace("RecvFrom on virtual socket via proxy");
            // Receive from RyuLDN proxy
            sockaddr_in* src_addr = addr.GetSize() >= sizeof(sockaddr_in) ?
                                    reinterpret_cast<sockaddr_in*>(addr.GetPointer()) : nullptr;

            if (!src_addr) {
                LogWarning("RecvFrom virtual: addr buffer too small (%zu bytes)", addr.GetSize());
            }

            size_t received = 0;
            Result rc = s_proxy->RecvFrom(fd,
                                         reinterpret_cast<u8*>(buf.GetPointer()),
                                         buf.GetSize(),
                                         &received,
                                         src_addr);

            if (R_SUCCEEDED(rc) && received > 0) {
                out_ret.SetValue(received);
                out_errno.SetValue(0);
                out_addrlen.SetValue(sizeof(sockaddr_in));

                LogNetRecv(fd, received);
                LogInfo("BSD RecvFrom virtual socket: fd=%d, size=%zu, from=0x%08x:%u",
                        fd, received,
                        src_addr ? ntohl(src_addr->sin_addr.s_addr) : 0,
                        src_addr ? ntohs(src_addr->sin_port) : 0);

                return ResultSuccess();
            }

            // No data available
            LogTrace("RecvFrom virtual: no data (EWOULDBLOCK)");
            out_ret.SetValue(-1);
            out_errno.SetValue(EWOULDBLOCK);
            out_addrlen.SetValue(0);
            return ResultSuccess();
        }

        // Normal recvfrom - forward to real BSD service
        LogTrace("Forwarding RecvFrom to real BSD");
        struct {
            s32 fd;
            u32 flags;
        } in_args = { fd, flags };

        struct {
            s32 ret;
            u32 errno_val;
            u32 addrlen;
        } out_args = {};

        Result rc = serviceDispatchInOut(m_forward_service.get(), 9, in_args, out_args,
            .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias, SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
            .buffers = { { buf.GetPointer(), buf.GetSize() }, { addr.GetPointer(), addr.GetSize() } },
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            out_addrlen.SetValue(out_args.addrlen);
            if (out_args.ret >= 0) {
                LogNetRecv(fd, out_args.ret);
            } else if (out_args.errno_val != EWOULDBLOCK && out_args.errno_val != EAGAIN) {
                LogNetError(fd, out_args.errno_val, "RecvFrom failed");
            }
        } else {
            LogError("RecvFrom IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Close(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd) {
        LogFunctionEntry();
        LogDebug("Close request: fd=%d", fd);

        {
            std::scoped_lock lk(socket_map_mutex);
            SocketEntry* entry = GetSocketEntry(fd);

            if (entry && entry->type == SocketType::Virtual) {
                LogInfo("BSD Close VIRTUAL socket: fd=%d", fd);

                // Notify proxy to clean up state for this FD
                if (s_proxy) {
                    LogDebug("Notifying proxy to cleanup socket fd=%d", fd);
                    s_proxy->CleanupSocket(fd);
                } else {
                    LogWarning("Close virtual socket but no proxy available!");
                }

                // Reset entry
                entry->type = SocketType::Real;
                entry->virtual_socket = nullptr;
                LogStateChange("socket=virtual", "socket=closed");
            } else if (entry) {
                LogDebug("Closing real socket: fd=%d", fd);
            }
        }

        // Always forward close to real BSD
        LogTrace("Forwarding Close to real BSD");
        struct {
            s32 fd;
        } in_args = { fd };

        struct {
            s32 ret;
            u32 errno_val;
        } out_args = {};

        Result rc = serviceDispatchInOut(m_forward_service.get(), 26, in_args, out_args);

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            if (out_args.ret == 0) {
                LogNetClose(fd);
                LogInfo("Close succeeded: fd=%d", fd);
            } else {
                LogWarning("Close failed: fd=%d, ret=%d, errno=%u", fd, out_args.ret, out_args.errno_val);
            }
        } else {
            LogError("Close IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }
}
