#include "bsd_mitm_service.hpp"
#include "ryuldn/proxy/ldn_proxy_socket.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <algorithm>
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <vector>

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

    bool BsdMitmService::EnsureProxyAvailable(sf::Out<s32> out_ret, sf::Out<u32> out_errno, const char* context) {
        if (s_proxy) {
            return true;
        }

        LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "%s: proxy unavailable", context);
        out_ret.SetValue(-1);
        out_errno.SetValue(EHOSTUNREACH);
        return false;
    }

    ryuldn::proxy::LdnProxySocket* BsdMitmService::GetVirtualSocket(s32 fd) {
        SocketEntry* entry = GetSocketEntry(fd);
        if (!entry) {
            return nullptr;
        }

        auto* socket = reinterpret_cast<ryuldn::proxy::LdnProxySocket*>(entry->virtual_socket);
        if (socket) {
            return socket;
        }

        if (!s_proxy) {
            return nullptr;
        }

        // Create a new virtual socket for this fd
        socket = new ryuldn::proxy::LdnProxySocket(static_cast<s32>(entry->address_family),
                                                   static_cast<s32>(entry->socket_type),
                                                   static_cast<s32>(entry->protocol_type),
                                                   s_proxy);
        s_proxy->RegisterSocket(socket);
        entry->virtual_socket = socket;
        return socket;
    }

    static void ClearFd(fd_set* set, int fd) {
        if (set) {
            FD_CLR(fd, set);
        }
    }

    static void SetFd(fd_set* set, int fd) {
        if (set) {
            FD_SET(fd, set);
        }
    }

    static bool TestFd(const fd_set* set, int fd) {
        return set && FD_ISSET(fd, set);
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
            socket_map[real_fd].address_family = domain;
            socket_map[real_fd].socket_type = type;
            socket_map[real_fd].protocol_type = protocol;

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

                // Check if this should be a virtual socket:
                // 1. Binding to a specific virtual IP, OR
                // 2. Binding to INADDR_ANY (0.0.0.0) with a proxy available (for broadcast reception)
                bool isVirtualBind = (sa->sin_addr.s_addr != INADDR_ANY && IsRyuLdnVirtualIP(ntohl(sa->sin_addr.s_addr))) ||
                                     (sa->sin_addr.s_addr == INADDR_ANY && s_proxy != nullptr);

                if (isVirtualBind) {
                    LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD Bind to VIRTUAL address: 0x%08x:%u",
                            ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));

                    if (!EnsureProxyAvailable(out_ret, out_errno, "Bind")) {
                        return ResultSuccess();
                    }

                    // Mark this socket as virtual
                    std::scoped_lock lk(socket_map_mutex);
                    SocketEntry* entry = GetSocketEntry(fd);
                    if (entry) {
                        entry->type = SocketType::Virtual;
                        auto* vsock = GetVirtualSocket(fd);
                        if (vsock) {
                            // For INADDR_ANY, create a local endpoint with the proxy IP
                            sockaddr_in virtualAddr = *sa;
                            if (sa->sin_addr.s_addr == INADDR_ANY) {
                                // Bind to proxy IP instead of 0.0.0.0 for virtual reception
                                virtualAddr.sin_addr.s_addr = htonl(s_proxy->GetLocalIP());
                                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "Converting INADDR_ANY bind to proxy IP: 0x%08x:%u",
                                        s_proxy->GetLocalIP(), ntohs(sa->sin_port));
                            }
                            vsock->Bind(&virtualAddr);
                        }
                        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "NET Bind fd=%d port=%u", fd, ntohs(sa->sin_port)); 
                        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "socket=real", "socket=virtual");
                    } else {
                        LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Bind: Failed to get socket entry for fd=%d", fd);
                        out_ret.SetValue(-1);
                        out_errno.SetValue(EBADF);
                        return ResultSuccess();
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

                    if (!EnsureProxyAvailable(out_ret, out_errno, "Connect")) {
                        return ResultSuccess();
                    }

                    // Mark this socket as virtual
                    std::scoped_lock lk(socket_map_mutex);
                    SocketEntry* entry = GetSocketEntry(fd);
                    if (entry) {
                        entry->type = SocketType::Virtual;
                        auto* vsock = GetVirtualSocket(fd);
                        if (vsock) {
                            vsock->Connect(sa);
                        }
                        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "NET Connect fd=%d addr=VIRTUAL port=%u", fd, ntohs(sa->sin_port)); 
                        LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "socket=real", "socket=virtual");
                    } else {
                        LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Connect: Failed to get socket entry for fd=%d", fd);
                        out_ret.SetValue(-1);
                        out_errno.SetValue(EBADF);
                        return ResultSuccess();
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

    Result BsdMitmService::Select(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 nfds, sf::InAutoSelectBuffer readfds, sf::InAutoSelectBuffer writefds, sf::InAutoSelectBuffer exceptfds, sf::InAutoSelectBuffer timeout) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Select: nfds=%d", nfds);

        fd_set* in_read = readfds.GetSize() ? const_cast<fd_set*>(reinterpret_cast<const fd_set*>(readfds.GetPointer())) : nullptr;
        fd_set* in_write = writefds.GetSize() ? const_cast<fd_set*>(reinterpret_cast<const fd_set*>(writefds.GetPointer())) : nullptr;
        fd_set* in_except = exceptfds.GetSize() ? const_cast<fd_set*>(reinterpret_cast<const fd_set*>(exceptfds.GetPointer())) : nullptr;

        fd_set out_read, out_write, out_except;
        FD_ZERO(&out_read);
        FD_ZERO(&out_write);
        FD_ZERO(&out_except);

        fd_set fwd_read, fwd_write, fwd_except;
        FD_ZERO(&fwd_read);
        FD_ZERO(&fwd_write);
        FD_ZERO(&fwd_except);

        if (in_read)   std::memcpy(&fwd_read, in_read, std::min(readfds.GetSize(), sizeof(fd_set)));
        if (in_write)  std::memcpy(&fwd_write, in_write, std::min(writefds.GetSize(), sizeof(fd_set)));
        if (in_except) std::memcpy(&fwd_except, in_except, std::min(exceptfds.GetSize(), sizeof(fd_set)));

        if (nfds < 0) {
            out_ret.SetValue(-1);
            out_errno.SetValue(EINVAL);
            return ResultSuccess();
        }

        std::vector<bool> ready(static_cast<size_t>(nfds), false);
        s32 ready_count = 0;
        bool has_real = false;

        {
            std::scoped_lock lk(socket_map_mutex);
            for (s32 fd = 0; fd < nfds; ++fd) {
                SocketEntry* entry = GetSocketEntry(fd);
                if (!entry || entry->type != SocketType::Virtual) {
                    if (TestFd(in_read, fd) || TestFd(in_write, fd) || TestFd(in_except, fd)) {
                        has_real = true;
                    }
                    continue;
                }

                auto* vsock = GetVirtualSocket(fd);
                if (!vsock) {
                    continue;
                }

                bool readable = vsock->IsReadable();
                bool writable = vsock->IsWritable();
                bool has_error = vsock->HasError();

                if (TestFd(in_read, fd) && readable) {
                    SetFd(&out_read, fd);
                    ready[fd] = true;
                }
                if (TestFd(in_write, fd) && writable) {
                    SetFd(&out_write, fd);
                    ready[fd] = true;
                }
                if (TestFd(in_except, fd) && has_error) {
                    SetFd(&out_except, fd);
                    ready[fd] = true;
                }

                // Remove virtual fd from forwarding sets
                ClearFd(&fwd_read, fd);
                ClearFd(&fwd_write, fd);
                ClearFd(&fwd_except, fd);
            }
        }

        for (s32 fd = 0; fd < nfds; ++fd) {
            if (ready[fd]) {
                ready_count++;
            }
        }

        // Forward to real BSD if any real fds remain
        if (has_real) {
            struct BsdSelectTimeval {
                timeval tv;
                bool is_null;
            } tv_in{};

            if (timeout.GetPointer() && timeout.GetSize() >= sizeof(timeval)) {
                tv_in.tv = *reinterpret_cast<const timeval*>(timeout.GetPointer());
                tv_in.is_null = false;
            } else {
                tv_in.is_null = true;
            }

            struct {
                s32 nfds;
                BsdSelectTimeval timeout;
            } in_args = { nfds, tv_in };

            struct {
                s32 ret;
                u32 errno_val;
            } out_args = {};

            Result rc = serviceDispatchInOut(m_forward_service.get(), 5, in_args, out_args,
                .buffer_attrs = {
                    SfBufferAttr_In | SfBufferAttr_Out | SfBufferAttr_HipcMapAlias,
                    SfBufferAttr_In | SfBufferAttr_Out | SfBufferAttr_HipcMapAlias,
                    SfBufferAttr_In | SfBufferAttr_Out | SfBufferAttr_HipcMapAlias,
                    SfBufferAttr_In | SfBufferAttr_Out | SfBufferAttr_HipcMapAlias,
                    SfBufferAttr_In | SfBufferAttr_Out | SfBufferAttr_HipcMapAlias,
                    SfBufferAttr_In | SfBufferAttr_Out | SfBufferAttr_HipcMapAlias,
                },
                .buffers = {
                    { &fwd_read,   sizeof(fd_set) },
                    { &fwd_write,  sizeof(fd_set) },
                    { &fwd_except, sizeof(fd_set) },
                    { &fwd_read,   sizeof(fd_set) },
                    { &fwd_write,  sizeof(fd_set) },
                    { &fwd_except, sizeof(fd_set) },
                },
            );

            if (R_FAILED(rc)) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Select IPC failed: rc=0x%x", rc.GetValue());
                return rc;
            }

            if (out_args.ret < 0) {
                out_ret.SetValue(out_args.ret);
                out_errno.SetValue(out_args.errno_val);
                return ResultSuccess();
            }

            for (s32 fd = 0; fd < nfds; ++fd) {
                if (TestFd(&fwd_read, fd)) {
                    SetFd(&out_read, fd);
                    if (!ready[fd]) { ready_count++; ready[fd] = true; }
                }
                if (TestFd(&fwd_write, fd)) {
                    SetFd(&out_write, fd);
                    if (!ready[fd]) { ready_count++; ready[fd] = true; }
                }
                if (TestFd(&fwd_except, fd)) {
                    SetFd(&out_except, fd);
                    if (!ready[fd]) { ready_count++; ready[fd] = true; }
                }
            }
        }

        if (in_read && readfds.GetSize() >= sizeof(fd_set)) {
            std::memcpy(in_read, &out_read, sizeof(fd_set));
        }
        if (in_write && writefds.GetSize() >= sizeof(fd_set)) {
            std::memcpy(in_write, &out_write, sizeof(fd_set));
        }
        if (in_except && exceptfds.GetSize() >= sizeof(fd_set)) {
            std::memcpy(in_except, &out_except, sizeof(fd_set));
        }

        out_ret.SetValue(ready_count);
        out_errno.SetValue(0);
        return ResultSuccess();
    }

    Result BsdMitmService::Poll(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::InAutoSelectBuffer fds_buf, u32 nfds, s32 timeout_ms) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Poll: nfds=%u, timeout=%d", nfds, timeout_ms);

        if (nfds == 0) {
            out_ret.SetValue(0);
            out_errno.SetValue(0);
            return ResultSuccess();
        }

        if (fds_buf.GetSize() < nfds * sizeof(struct pollfd)) {
            out_ret.SetValue(-1);
            out_errno.SetValue(EINVAL);
            return ResultSuccess();
        }

        auto* pollfds = const_cast<struct pollfd*>(reinterpret_cast<const struct pollfd*>(fds_buf.GetPointer()));

        std::vector<struct pollfd> real_fds;
        std::vector<size_t> real_indices;
        real_fds.reserve(nfds);
        real_indices.reserve(nfds);

        s32 ready_count = 0;
        bool has_real = false;

        {
            std::scoped_lock lk(socket_map_mutex);
            for (u32 i = 0; i < nfds; ++i) {
                struct pollfd& pfd = pollfds[i];
                SocketEntry* entry = GetSocketEntry(pfd.fd);
                if (entry && entry->type == SocketType::Virtual) {
                    if (!EnsureProxyAvailable(out_ret, out_errno, "Poll")) {
                        return ResultSuccess();
                    }
                    auto* vsock = GetVirtualSocket(pfd.fd);
                    if (!vsock) {
                        continue;
                    }

                    short revents = 0;
                    if (pfd.events & (POLLIN | POLLRDNORM | POLLRDBAND)) {
                        if (vsock->IsReadable()) revents |= POLLIN | POLLRDNORM;
                    }
                    if (pfd.events & (POLLOUT | POLLWRNORM | POLLWRBAND)) {
                        if (vsock->IsWritable()) revents |= POLLOUT | POLLWRNORM;
                    }
                    if (vsock->HasError()) {
                        revents |= POLLERR;
                    }

                    pfd.revents = revents;
                    if (revents != 0) {
                        ready_count++;
                    }
                } else {
                    has_real = true;
                    real_fds.push_back(pfd);
                    real_indices.push_back(i);
                }
            }
        }

        if (has_real && !real_fds.empty()) {
            struct {
                s32 ret;
                u32 errno_val;
            } out_args = {};

            struct {
                nfds_t nfds;
                s32 timeout;
            } in_args = { static_cast<nfds_t>(real_fds.size()), timeout_ms };

            Result rc = serviceDispatchInOut(m_forward_service.get(), 6, in_args, out_args,
                .buffer_attrs = {
                    SfBufferAttr_In  | SfBufferAttr_HipcMapAlias,
                    SfBufferAttr_Out | SfBufferAttr_HipcMapAlias,
                },
                .buffers = {
                    { real_fds.data(), real_fds.size() * sizeof(struct pollfd) },
                    { real_fds.data(), real_fds.size() * sizeof(struct pollfd) },
                },
            );

            if (R_FAILED(rc)) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Poll IPC failed: rc=0x%x", rc.GetValue());
                return rc;
            }

            if (out_args.ret < 0) {
                out_ret.SetValue(out_args.ret);
                out_errno.SetValue(out_args.errno_val);
                return ResultSuccess();
            }

            // Merge real poll results back
            for (size_t idx = 0; idx < real_indices.size(); ++idx) {
                pollfds[real_indices[idx]].revents = real_fds[idx].revents;
                if (real_fds[idx].revents != 0) {
                    ready_count++;
                }
            }
        }

        out_ret.SetValue(ready_count);
        out_errno.SetValue(0);
        return ResultSuccess();
    }

    Result BsdMitmService::Send(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Send: fd=%d, size=%zu, flags=0x%x", fd, data.GetSize(), flags);

        std::scoped_lock lk(socket_map_mutex);
        SocketEntry* entry = GetSocketEntry(fd);

        if (entry && entry->type == SocketType::Virtual) {
            if (!EnsureProxyAvailable(out_ret, out_errno, "Send")) {
                return ResultSuccess();
            }

            ryuldn::proxy::LdnProxySocket* vsock = GetVirtualSocket(fd);
            if (vsock) {
                s32 sent = vsock->Send(reinterpret_cast<const u8*>(data.GetPointer()), data.GetSize(), flags);
                if (sent >= 0) {
                    out_ret.SetValue(sent);
                    out_errno.SetValue(0);
                    LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "NET Send fd=%d bytes=%d", fd, sent);
                    return ResultSuccess();
                }
            }

            // Can't send without destination for UDP or send failed
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

                    if (!EnsureProxyAvailable(out_ret, out_errno, "SendTo")) {
                        return ResultSuccess();
                    }

                    ryuldn::proxy::LdnProxySocket* vsock = nullptr;
                    {
                        std::scoped_lock lk(socket_map_mutex);
                        vsock = GetVirtualSocket(fd);
                    }

                    if (vsock) {
                        s32 sent = vsock->SendTo(reinterpret_cast<const u8*>(data.GetPointer()), data.GetSize(), 0, dest);
                        if (sent >= 0) {
                            out_ret.SetValue(sent);
                            out_errno.SetValue(0);
                            LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] SendTo fd=%d bytes=%d", fd, sent);
                            return ResultSuccess();
                        }
                    }

                    LOG_WARN(COMP_BSD_MITM_SVC, "Proxy SendTo failed: virtual socket send error");

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

        if (entry && entry->type == SocketType::Virtual) {
            if (!EnsureProxyAvailable(out_ret, out_errno, "Recv")) {
                return ResultSuccess();
            }
            LOG_TRACE(COMP_BSD_MITM_SVC, "Recv on virtual socket via proxy");
            // Recv() without address buffer - receive via proxy
            ryuldn::proxy::LdnProxySocket* vsock = GetVirtualSocket(fd);
            if (vsock) {
                s32 received = vsock->Receive(reinterpret_cast<u8*>(buf.GetPointer()), buf.GetSize(), flags);

                if (received > 0) {
                    out_ret.SetValue(received);
                    out_errno.SetValue(0);
                    LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] Recv fd=%d bytes=%d", fd, received);
                    LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD Recv virtual socket: fd=%d, size=%d", fd, received);
                    return ResultSuccess();
                }
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

        if (entry && entry->type == SocketType::Virtual) {
            if (!EnsureProxyAvailable(out_ret, out_errno, "RecvFrom")) {
                out_addrlen.SetValue(0);
                return ResultSuccess();
            }
            LOG_TRACE(COMP_BSD_MITM_SVC, "RecvFrom on virtual socket via proxy");
            // Receive from RyuLDN proxy
            sockaddr_in* src_addr = addr.GetSize() >= sizeof(sockaddr_in) ?
                                    reinterpret_cast<sockaddr_in*>(addr.GetPointer()) : nullptr;

            if (!src_addr) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "RecvFrom virtual: addr buffer too small (%zu bytes)", addr.GetSize());
            }

            ryuldn::proxy::LdnProxySocket* vsock = GetVirtualSocket(fd);
            if (vsock) {
                s32 received = vsock->ReceiveFrom(reinterpret_cast<u8*>(buf.GetPointer()), buf.GetSize(), flags, src_addr);

                if (received > 0) {
                    out_ret.SetValue(received);
                    out_errno.SetValue(0);
                    out_addrlen.SetValue(sizeof(sockaddr_in));

                    LOG_DBG_ARGS(COMP_BSD_MITM_SVC, "[NET] RecvFrom fd=%d bytes=%d", fd, received);
                    LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "BSD RecvFrom virtual socket: fd=%d, size=%d, from=0x%08x:%u",
                            fd, received,
                            src_addr ? ntohl(src_addr->sin_addr.s_addr) : 0,
                            src_addr ? ntohs(src_addr->sin_port) : 0);

                    return ResultSuccess();
                }
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

                // Delete virtual socket instance
                auto* vsock = reinterpret_cast<ryuldn::proxy::LdnProxySocket*>(entry->virtual_socket);
                if (vsock && s_proxy) {
                    s_proxy->UnregisterSocket(vsock);
                }
                delete vsock;

                // Reset entry
                entry->type = SocketType::Real;
                entry->virtual_socket = nullptr;
                entry->address_family = 0;
                entry->socket_type = 0;
                entry->protocol_type = 0;
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

    Result BsdMitmService::Accept(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer addr) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Accept: fd=%d, addr_size=%zu", fd, addr.GetSize());

        SocketEntry* entry = GetSocketEntry(fd);
        if (!entry || entry->type == SocketType::Real) {
            LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "Accept: Invalid fd=%d", fd);
            out_ret.SetValue(-1);
            out_errno.SetValue(EBADF);
            out_addrlen.SetValue(0);
            return ResultSuccess();
        }

        if (entry->type == SocketType::Virtual) {
            if (!EnsureProxyAvailable(out_ret, out_errno, "Accept")) {
                out_addrlen.SetValue(0);
                return ResultSuccess();
            }

            auto virtual_socket = GetVirtualSocket(fd);
            if (!virtual_socket) {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "Accept: Failed to get virtual socket for fd=%d", fd);
                out_ret.SetValue(-1);
                out_errno.SetValue(ENOTCONN);
                out_addrlen.SetValue(0);
                return ResultSuccess();
            }

            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_fd = virtual_socket->BsdAccept(reinterpret_cast<sockaddr*>(&client_addr), &client_len);

            if (new_fd < 0) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Accept failed: errno=%d", errno);
                out_ret.SetValue(-1);
                out_errno.SetValue(errno);
                out_addrlen.SetValue(0);
            } else {
                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "Accept succeeded: new_fd=%d", new_fd);
                out_ret.SetValue(new_fd);
                out_errno.SetValue(0);
                out_addrlen.SetValue(client_len);
                
                if (addr.GetSize() >= client_len) {
                    std::memcpy(addr.GetPointer(), &client_addr, client_len);
                }

                // Register new virtual socket
                SocketEntry* new_entry = GetSocketEntry(new_fd);
                if (new_entry) {
                    new_entry->type = SocketType::Virtual;
                    new_entry->address_family = entry->address_family;
                    new_entry->socket_type = entry->socket_type;
                    new_entry->protocol_type = entry->protocol_type;
                }
            }
            return ResultSuccess();
        }

        // Forward to real BSD service
        struct {
            s32 ret;
            u32 errno_val;
            u32 addrlen;
        } out_args;

        struct {
            s32 fd;
        } in_args = { fd };

        Result rc = serviceDispatchInOut(
            this->m_forward_service.get(),
            12,
            in_args,
            out_args,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
            .buffers = { { addr.GetPointer(), addr.GetSize() } }
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            out_addrlen.SetValue(out_args.addrlen);
            LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Accept forwarded: ret=%d, errno=%u", out_args.ret, out_args.errno_val);
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Accept IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::GetSockOpt(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_optlen, s32 fd, s32 level, s32 optname, sf::OutAutoSelectBuffer optval) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "GetSockOpt: fd=%d, level=%d, optname=%d", fd, level, optname);

        SocketEntry* entry = GetSocketEntry(fd);
        if (!entry || entry->type == SocketType::Real) {
            LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "GetSockOpt: Invalid fd=%d", fd);
            out_ret.SetValue(-1);
            out_errno.SetValue(EBADF);
            out_optlen.SetValue(0);
            return ResultSuccess();
        }

        if (entry->type == SocketType::Virtual) {
            if (!EnsureProxyAvailable(out_ret, out_errno, "GetSockOpt")) {
                out_optlen.SetValue(0);
                return ResultSuccess();
            }

            auto virtual_socket = GetVirtualSocket(fd);
            if (!virtual_socket) {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "GetSockOpt: Failed to get virtual socket for fd=%d", fd);
                out_ret.SetValue(-1);
                out_errno.SetValue(ENOTCONN);
                out_optlen.SetValue(0);
                return ResultSuccess();
            }

            socklen_t len = static_cast<socklen_t>(optval.GetSize());
            int result = virtual_socket->BsdGetSocketOption(level, optname, optval.GetPointer(), &len);

            if (result < 0) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "GetSockOpt failed: errno=%d", errno);
                out_ret.SetValue(-1);
                out_errno.SetValue(errno);
                out_optlen.SetValue(0);
            } else {
                out_ret.SetValue(0);
                out_errno.SetValue(0);
                out_optlen.SetValue(len);
                LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "GetSockOpt succeeded: optlen=%u", len);
            }
            return ResultSuccess();
        }

        // Forward to real BSD service
        struct {
            s32 ret;
            u32 errno_val;
            u32 optlen;
        } out_args;

        struct {
            s32 fd;
            s32 level;
            s32 optname;
        } in_args = { fd, level, optname };

        Result rc = serviceDispatchInOut(
            this->m_forward_service.get(),
            17,
            in_args,
            out_args,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
            .buffers = { { optval.GetPointer(), optval.GetSize() } }
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            out_optlen.SetValue(out_args.optlen);
            LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "GetSockOpt forwarded: ret=%d, errno=%u", out_args.ret, out_args.errno_val);
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "GetSockOpt IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Listen(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 backlog) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Listen: fd=%d, backlog=%d", fd, backlog);

        SocketEntry* entry = GetSocketEntry(fd);
        if (!entry || entry->type == SocketType::Real) {
            LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "Listen: Invalid fd=%d", fd);
            out_ret.SetValue(-1);
            out_errno.SetValue(EBADF);
            return ResultSuccess();
        }

        if (entry->type == SocketType::Virtual) {
            if (!EnsureProxyAvailable(out_ret, out_errno, "Listen")) {
                return ResultSuccess();
            }

            auto virtual_socket = GetVirtualSocket(fd);
            if (!virtual_socket) {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "Listen: Failed to get virtual socket for fd=%d", fd);
                out_ret.SetValue(-1);
                out_errno.SetValue(ENOTCONN);
                return ResultSuccess();
            }

            int result = virtual_socket->BsdListen(backlog);

            if (result < 0) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Listen failed: errno=%d", errno);
                out_ret.SetValue(-1);
                out_errno.SetValue(errno);
            } else {
                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "Listen succeeded: fd=%d", fd);
                out_ret.SetValue(0);
                out_errno.SetValue(0);
            }
            return ResultSuccess();
        }

        // Forward to real BSD service
        struct {
            s32 ret;
            u32 errno_val;
        } out_args;

        struct {
            s32 fd;
            s32 backlog;
        } in_args = { fd, backlog };

        Result rc = serviceDispatchInOut(
            this->m_forward_service.get(),
            18,
            in_args,
            out_args
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Listen forwarded: ret=%d, errno=%u", out_args.ret, out_args.errno_val);
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Listen IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Fcntl(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 cmd, s32 flags) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Fcntl: fd=%d, cmd=%d, flags=0x%x", fd, cmd, flags);

        SocketEntry* entry = GetSocketEntry(fd);
        if (!entry || entry->type == SocketType::Real) {
            LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "Fcntl: Invalid fd=%d", fd);
            out_ret.SetValue(-1);
            out_errno.SetValue(EBADF);
            return ResultSuccess();
        }

        if (entry->type == SocketType::Virtual) {
            if (!EnsureProxyAvailable(out_ret, out_errno, "Fcntl")) {
                return ResultSuccess();
            }

            auto virtual_socket = GetVirtualSocket(fd);
            if (!virtual_socket) {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "Fcntl: Failed to get virtual socket for fd=%d", fd);
                out_ret.SetValue(-1);
                out_errno.SetValue(ENOTCONN);
                return ResultSuccess();
            }

            int result = virtual_socket->BsdFcntl(cmd, flags);

            if (result < 0) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Fcntl failed: errno=%d", errno);
                out_ret.SetValue(-1);
                out_errno.SetValue(errno);
            } else {
                LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Fcntl succeeded: result=%d", result);
                out_ret.SetValue(result);
                out_errno.SetValue(0);
            }
            return ResultSuccess();
        }

        // Forward to real BSD service
        struct {
            s32 ret;
            u32 errno_val;
        } out_args;

        struct {
            s32 fd;
            s32 cmd;
            s32 flags;
        } in_args = { fd, cmd, flags };

        Result rc = serviceDispatchInOut(
            this->m_forward_service.get(),
            20,
            in_args,
            out_args
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Fcntl forwarded: ret=%d, errno=%u", out_args.ret, out_args.errno_val);
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Fcntl IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::SetSockOpt(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 level, s32 optname, sf::InAutoSelectBuffer optval) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "SetSockOpt: fd=%d, level=%d, optname=%d", fd, level, optname);

        SocketEntry* entry = GetSocketEntry(fd);
        if (!entry || entry->type == SocketType::Real) {
            LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "SetSockOpt: Invalid fd=%d", fd);
            out_ret.SetValue(-1);
            out_errno.SetValue(EBADF);
            return ResultSuccess();
        }

        if (entry->type == SocketType::Virtual) {
            if (!EnsureProxyAvailable(out_ret, out_errno, "SetSockOpt")) {
                return ResultSuccess();
            }

            auto virtual_socket = GetVirtualSocket(fd);
            if (!virtual_socket) {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "SetSockOpt: Failed to get virtual socket for fd=%d", fd);
                out_ret.SetValue(-1);
                out_errno.SetValue(ENOTCONN);
                return ResultSuccess();
            }

            int result = virtual_socket->BsdSetSocketOption(level, optname, optval.GetPointer(), static_cast<socklen_t>(optval.GetSize()));

            if (result < 0) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "SetSockOpt failed: errno=%d", errno);
                out_ret.SetValue(-1);
                out_errno.SetValue(errno);
            } else {
                LOG_TRACE(COMP_BSD_MITM_SVC, "SetSockOpt succeeded");
                out_ret.SetValue(0);
                out_errno.SetValue(0);
            }
            return ResultSuccess();
        }

        // Forward to real BSD service
        struct {
            s32 ret;
            u32 errno_val;
        } out_args;

        struct {
            s32 fd;
            s32 level;
            s32 optname;
        } in_args = { fd, level, optname };

        Result rc = serviceDispatchInOut(
            this->m_forward_service.get(),
            21,
            in_args,
            out_args,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
            .buffers = { { optval.GetPointer(), optval.GetSize() } }
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "SetSockOpt forwarded: ret=%d, errno=%u", out_args.ret, out_args.errno_val);
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "SetSockOpt IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }

    Result BsdMitmService::Shutdown(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 how) {
        LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Shutdown: fd=%d, how=%d", fd, how);

        SocketEntry* entry = GetSocketEntry(fd);
        if (!entry || entry->type == SocketType::Real) {
            LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "Shutdown: Invalid fd=%d", fd);
            out_ret.SetValue(-1);
            out_errno.SetValue(EBADF);
            return ResultSuccess();
        }

        if (entry->type == SocketType::Virtual) {
            if (!EnsureProxyAvailable(out_ret, out_errno, "Shutdown")) {
                return ResultSuccess();
            }

            auto virtual_socket = GetVirtualSocket(fd);
            if (!virtual_socket) {
                LOG_ERR_ARGS(COMP_BSD_MITM_SVC, "Shutdown: Failed to get virtual socket for fd=%d", fd);
                out_ret.SetValue(-1);
                out_errno.SetValue(ENOTCONN);
                return ResultSuccess();
            }

            int result = virtual_socket->BsdShutdown(how);

            if (result < 0) {
                LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Shutdown failed: errno=%d", errno);
                out_ret.SetValue(-1);
                out_errno.SetValue(errno);
            } else {
                LOG_INFO_ARGS(COMP_BSD_MITM_SVC, "Shutdown succeeded: fd=%d, how=%d", fd, how);
                out_ret.SetValue(0);
                out_errno.SetValue(0);
            }
            return ResultSuccess();
        }

        // Forward to real BSD service
        struct {
            s32 ret;
            u32 errno_val;
        } out_args;

        struct {
            s32 fd;
            s32 how;
        } in_args = { fd, how };

        Result rc = serviceDispatchInOut(
            this->m_forward_service.get(),
            22,
            in_args,
            out_args
        );

        if (R_SUCCEEDED(rc)) {
            out_ret.SetValue(out_args.ret);
            out_errno.SetValue(out_args.errno_val);
            LOG_TRACE_ARGS(COMP_BSD_MITM_SVC, "Shutdown forwarded: ret=%d, errno=%u", out_args.ret, out_args.errno_val);
        } else {
            LOG_WARN_ARGS(COMP_BSD_MITM_SVC, "Shutdown IPC failed: rc=0x%x", rc.GetValue());
        }
        return rc;
    }
}
