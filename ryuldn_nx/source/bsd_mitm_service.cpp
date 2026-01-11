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
        LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BsdMitmService created for process: pid=%" PRIu64 ", program_id=0x%016lx",
                c.process_id, c.program_id.value);
        std::memset(socket_map, 0, sizeof(socket_map));
        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "Socket map initialized (%zu entries)", MaxSockets);
    }

    BsdMitmService::~BsdMitmService() {
        LOG_INFO(COMP_BSD_MITM_SVC, "BsdMitmService destroyed");

        std::scoped_lock lk(socket_map_mutex);
        int virtual_count = 0, real_count = 0;
        for (size_t i = 0; i < MaxSockets; i++) {
            if (socket_map[i].type == SocketType::Virtual) {
                virtual_count++;
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Virtual socket still open at destruction: fd=%zu", i);
            } else if (socket_map[i].type == SocketType::Real) {
                real_count++;
            }
        }
        if (virtual_count > 0 || real_count > 0) {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Sockets at destruction: %d virtual, %d real", virtual_count, real_count);
        }
    }

    bool BsdMitmService::ShouldMitm(const sm::MitmProcessInfo &client_info) {
        AMS_UNUSED(client_info);
        // Only MITM if RyuLDN proxy is active
        bool should = s_proxy != nullptr;
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "BsdMitmService::ShouldMitm: program_id=0x%016lx -> %s",
                 client_info.program_id.value, should ? "YES" : "NO");
        return should;
    }

    void BsdMitmService::RegisterProxy(ryuldn::proxy::LdnProxy* proxy) {
        LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BsdMitmService: Registering RyuLDN proxy at %p", (void*)proxy);
        if (!proxy) {
            LOG_WARN(COMP_BSD_MITM_SVC, "Null proxy being registered!");
        }
        s_proxy = proxy;
        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "proxy=null", "proxy=active");
    }

    void BsdMitmService::UnregisterProxy() {
        LOG_INFO(COMP_BSD_MITM_SVC, "BsdMitmService: Unregistering RyuLDN proxy");
        s_proxy = nullptr;
        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "proxy=active", "proxy=null");
    }

    BsdMitmService::SocketEntry* BsdMitmService::GetSocketEntry(s32 fd) {
        if (fd < 0 || static_cast<size_t>(fd) >= MaxSockets) {
            if (fd < 0) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "GetSocketEntry: Invalid fd=%d (negative)", fd);
            } else {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "GetSocketEntry: fd=%d exceeds MaxSockets=%zu", fd, MaxSockets);
            }
            return nullptr;
        }
        return &socket_map[fd];
    }

    bool BsdMitmService::IsRyuLdnVirtualIP(u32 ip) {
        if (!s_proxy) {
            LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "IsRyuLdnVirtualIP: No proxy, IP 0x%08x -> NO", ip);
            return false;
        }

        // Check if IP is in the virtual subnet
        bool result = s_proxy->IsVirtualIP(ip);
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "IsRyuLdnVirtualIP: 0x%08x -> %s", ip, result ? "YES" : "NO");
        return result;
    }

    Result BsdMitmService::Socket(sf::Out<s32> out_fd, u32 domain, u32 type, u32 protocol) {
        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "Socket request: domain=%u, type=%u, protocol=%u", domain, type, protocol);

        // Forward to real BSD service using IPC
        struct {
            u32 domain;
            u32 type;
            u32 protocol;
        } in_args = { domain, type, protocol };

        s32 real_fd = -1;
        Result rc = serviceDispatchInOut(m_forward_service.get(), 2, in_args, real_fd);

        if (R_FAILED(rc)) {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "BSD Socket creation failed: rc=0x%x", rc.GetValue());
            out_fd.SetValue(-1);
            return rc;
        }

        // Register in our map
        std::scoped_lock lk(socket_map_mutex);
        if (real_fd >= 0 && static_cast<size_t>(real_fd) < MaxSockets) {
            socket_map[real_fd].type = SocketType::Real;
            socket_map[real_fd].real_fd = real_fd;
            socket_map[real_fd].virtual_socket = nullptr;

            LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "NET Socket fd=%d domain=%u type=%u proto=%u", real_fd, domain, type, protocol); 
            LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD Socket registered in map: fd=%d", real_fd);
        } else {
            if (real_fd < 0) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "BSD Socket returned negative fd=%d", real_fd);
            } else {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "BSD Socket fd=%d exceeds MaxSockets=%zu, cannot register!", real_fd, MaxSockets);
            }
        }

        out_fd.SetValue(real_fd);
        return ResultSuccess();
    }

    Result BsdMitmService::Bind(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr) {
        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "Bind request: fd=%d, addr_size=%zu", fd, addr.GetSize());

        // Check if binding to a virtual IP
        if (addr.GetSize() >= sizeof(sockaddr_in)) {
            const sockaddr_in* sa = reinterpret_cast<const sockaddr_in*>(addr.GetPointer());

            if (sa->sin_family == AF_INET) {
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "Bind address: family=%d, IP=0x%08x, port=%u",
                         sa->sin_family, ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));

                if (sa->sin_addr.s_addr != INADDR_ANY && IsRyuLdnVirtualIP(ntohl(sa->sin_addr.s_addr))) {
                    LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD Bind to VIRTUAL IP: 0x%08x:%u",
                            ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));

                    // Mark this socket as virtual
                    std::scoped_lock lk(socket_map_mutex);
                    SocketEntry* entry = GetSocketEntry(fd);
                    if (entry) {
                        entry->type = SocketType::Virtual;
                        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "NET Bind fd=%d port=%u", fd, ntohs(sa->sin_port)); 
                        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "socket=real", "socket=virtual");
                    } else {
                        LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Bind: Failed to get socket entry for fd=%d", fd);
                    }

                    // Pretend bind succeeded
                    out_ret.SetValue(0);
                    out_errno.SetValue(0);
                    return ResultSuccess();
                }
            }
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Bind: addr buffer size %zu < sizeof(sockaddr_in)", addr.GetSize());
        }

        // Normal bind - forward to real BSD service
        LOG_DBG(COMP_BSD_MITM_SVC, "Forwarding Bind to real BSD service");
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
                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "Bind succeeded: fd=%d, ret=%d", fd, out_args.ret);
            } else {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Bind failed: fd=%d, ret=%d, errno=%u", fd, out_args.ret, out_args.errno_val);
            }
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Bind IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Connect(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr) {
        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "Connect request: fd=%d, addr_size=%zu", fd, addr.GetSize());

        // Check if connecting to a virtual IP
        if (addr.GetSize() >= sizeof(sockaddr_in)) {
            const sockaddr_in* sa = reinterpret_cast<const sockaddr_in*>(addr.GetPointer());

            if (sa->sin_family == AF_INET) {
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "Connect to: IP=0x%08x, port=%u",
                         ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));

                if (IsRyuLdnVirtualIP(ntohl(sa->sin_addr.s_addr))) {
                    LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD Connect to VIRTUAL IP: 0x%08x:%u",
                            ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));

                    // Mark this socket as virtual
                    std::scoped_lock lk(socket_map_mutex);
                    SocketEntry* entry = GetSocketEntry(fd);
                    if (entry) {
                        entry->type = SocketType::Virtual;
                        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "NET Connect fd=%d addr=VIRTUAL port=%u", fd, ntohs(sa->sin_port)); 
                        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "socket=real", "socket=virtual");
                    } else {
                        LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Connect: Failed to get socket entry for fd=%d", fd);
                    }

                    // Pretend connect succeeded
                    out_ret.SetValue(0);
                    out_errno.SetValue(0);
                    return ResultSuccess();
                }
            }
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Connect: addr buffer size %zu < sizeof(sockaddr_in)", addr.GetSize());
        }

        // Normal connect - forward to real BSD service
        LOG_DBG(COMP_BSD_MITM_SVC, "Forwarding Connect to real BSD service");
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
                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "Connect succeeded: fd=%d", fd);
            } else {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Connect failed: fd=%d, ret=%d, errno=%u", fd, out_args.ret, out_args.errno_val);
            }
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Connect IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Send(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Send: fd=%d, size=%zu, flags=0x%x", fd, data.GetSize(), flags);

        std::scoped_lock lk(socket_map_mutex);
        SocketEntry* entry = GetSocketEntry(fd);

        if (entry && entry->type == SocketType::Virtual && s_proxy) {
            // Send() without address - this shouldn't happen for UDP sockets
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "BSD Send on virtual socket: fd=%d, size=%zu (no dest address!)",
                       fd, data.GetSize());

            // Can't send without destination for UDP, return error
            out_ret.SetValue(-1);
            out_errno.SetValue(ENOTCONN);
            return ResultSuccess();
        }

        // Normal send - forward to real BSD service
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Forwarding Send to real BSD: fd=%d, size=%zu", fd, data.GetSize());
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
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "NET Send fd=%d bytes=%d", fd, out_args.ret);
            } else {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "NET Send fd=%d err=%d (%d)", fd, out_args.errno_val);
            }
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Send IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::SendTo(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd,
                                   sf::InAutoSelectBuffer data, u32 flags, sf::InAutoSelectBuffer addr) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "SendTo: fd=%d, size=%zu, flags=0x%x, addr_size=%zu",
                 fd, data.GetSize(), flags, addr.GetSize());

        // Check destination address
        if (addr.GetSize() >= sizeof(sockaddr_in)) {
            const sockaddr_in* dest = reinterpret_cast<const sockaddr_in*>(addr.GetPointer());

            if (dest->sin_family == AF_INET) {
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "SendTo dest: IP=0x%08x, port=%u",
                         ntohl(dest->sin_addr.s_addr), ntohs(dest->sin_port));

                if (IsRyuLdnVirtualIP(ntohl(dest->sin_addr.s_addr))) {
                    LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD SendTo VIRTUAL IP: 0x%08x:%u, size=%zu",
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
                            LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] SendTo fd=%d bytes=%zu", fd, data.GetSize());
                            return ResultSuccess();
                        } else {
                            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Proxy SendTo failed: rc=0x%x", rc.GetValue());
                        }
                    } else {
                        LOG_WARN(COMP_BSD_MITM_SVC, "SendTo to virtual IP but no proxy available!");
                    }

                    // Failed
                    out_ret.SetValue(-1);
                    out_errno.SetValue(EHOSTUNREACH);
                    LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "[NET] fd=%d err=%d: Virtual SendTo failed", fd, EHOSTUNREACH);
                    return ResultSuccess();
                }
            }
        } else if (addr.GetSize() > 0) {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "SendTo: addr buffer size %zu < sizeof(sockaddr_in)", addr.GetSize());
        }

        // Normal sendto - forward to real BSD service
        LOG_TRACE(COMP_BSD_MITM_SVC, "Forwarding SendTo to real BSD");
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
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] SendTo fd=%d bytes=%d", fd, out_args.ret);
            } else {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "[NET] fd=%d err=%d: SendTo failed", fd, out_args.errno_val);
            }
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "SendTo IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Recv(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Recv: fd=%d, buf_size=%zu, flags=0x%x", fd, buf.GetSize(), flags);

        std::scoped_lock lk(socket_map_mutex);
        SocketEntry* entry = GetSocketEntry(fd);

        if (entry && entry->type == SocketType::Virtual && s_proxy) {
            LOG_TRACE(COMP_BSD_MITM_SVC, "Recv on virtual socket via proxy");
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
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] Recv fd=%d bytes=%zu", fd, received);
                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD Recv virtual socket: fd=%d, size=%zu", fd, received);
                return ResultSuccess();
            }

            // No data available
            LOG_TRACE(COMP_BSD_MITM_SVC, "Recv virtual: no data (EWOULDBLOCK)");
            out_ret.SetValue(-1);
            out_errno.SetValue(EWOULDBLOCK);
            return ResultSuccess();
        }

        // Normal recv - forward to real BSD service
        LOG_TRACE(COMP_BSD_MITM_SVC, "Forwarding Recv to real BSD");
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
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] Recv fd=%d bytes=%d", fd, out_args.ret);
            } else if (out_args.errno_val != EWOULDBLOCK && out_args.errno_val != EAGAIN) {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "[NET] fd=%d err=%d: Recv failed", fd, out_args.errno_val);
            }
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Recv IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::RecvFrom(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_addrlen,
                                     s32 fd, sf::OutAutoSelectBuffer buf, u32 flags, sf::OutAutoSelectBuffer addr) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "RecvFrom: fd=%d, buf_size=%zu, flags=0x%x, addr_size=%zu",
                 fd, buf.GetSize(), flags, addr.GetSize());

        std::scoped_lock lk(socket_map_mutex);
        SocketEntry* entry = GetSocketEntry(fd);

        if (entry && entry->type == SocketType::Virtual && s_proxy) {
            LOG_TRACE(COMP_BSD_MITM_SVC, "RecvFrom on virtual socket via proxy");
            // Receive from RyuLDN proxy
            sockaddr_in* src_addr = addr.GetSize() >= sizeof(sockaddr_in) ?
                                    reinterpret_cast<sockaddr_in*>(addr.GetPointer()) : nullptr;

            if (!src_addr) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "RecvFrom virtual: addr buffer too small (%zu bytes)", addr.GetSize());
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

                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] RecvFrom fd=%d bytes=%zu", fd, received);
                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD RecvFrom virtual socket: fd=%d, size=%zu, from=0x%08x:%u",
                        fd, received,
                        src_addr ? ntohl(src_addr->sin_addr.s_addr) : 0,
                        src_addr ? ntohs(src_addr->sin_port) : 0);

                return ResultSuccess();
            }

            // No data available
            LOG_TRACE(COMP_BSD_MITM_SVC, "RecvFrom virtual: no data (EWOULDBLOCK)");
            out_ret.SetValue(-1);
            out_errno.SetValue(EWOULDBLOCK);
            out_addrlen.SetValue(0);
            return ResultSuccess();
        }

        // Normal recvfrom - forward to real BSD service
        LOG_TRACE(COMP_BSD_MITM_SVC, "Forwarding RecvFrom to real BSD");
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
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] RecvFrom fd=%d bytes=%d", fd, out_args.ret);
            } else if (out_args.errno_val != EWOULDBLOCK && out_args.errno_val != EAGAIN) {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "[NET] fd=%d err=%d: RecvFrom failed", fd, out_args.errno_val);
            }
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "RecvFrom IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Close(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd) {
        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "Close request: fd=%d", fd);

        {
            std::scoped_lock lk(socket_map_mutex);
            SocketEntry* entry = GetSocketEntry(fd);

            if (entry && entry->type == SocketType::Virtual) {
                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD Close VIRTUAL socket: fd=%d", fd);

                // Notify proxy to clean up state for this FD
                if (s_proxy) {
                    LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "Notifying proxy to cleanup socket fd=%d", fd);
                    s_proxy->CleanupSocket(fd);
                } else {
                    LOG_WARN(COMP_BSD_MITM_SVC, "Close virtual socket but no proxy available!");
                }

                // Reset entry
                entry->type = SocketType::Real;
                entry->virtual_socket = nullptr;
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "socket=virtual", "socket=closed");
            } else if (entry) {
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "Closing real socket: fd=%d", fd);
            }
        }

        // Always forward close to real BSD
        LOG_TRACE(COMP_BSD_MITM_SVC, "Forwarding Close to real BSD");
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
                LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] Close fd=%d", fd);
                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "Close succeeded: fd=%d", fd);
            } else {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Close failed: fd=%d, ret=%d, errno=%u", fd, out_args.ret, out_args.errno_val);
            }
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Close IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }
}
