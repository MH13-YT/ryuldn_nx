#pragma once
#include <switch.h>
#include <stratosphere.hpp>
#include "debug.hpp"
#include "ryuldn/ryuldn.hpp"

// BSD:u IPC command IDs
#define AMS_BSD_MITM_INTERFACE_INFO(C, H)                                                                  \
    AMS_SF_METHOD_INFO(C, H,  2, Result, Socket,     (sf::Out<s32> out_fd, u32 domain, u32 type, u32 protocol), (out_fd, domain, type, protocol)) \
    AMS_SF_METHOD_INFO(C, H,  5, Result, Select,     (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 nfds, sf::InAutoSelectBuffer readfds, sf::InAutoSelectBuffer writefds, sf::InAutoSelectBuffer exceptfds, sf::InAutoSelectBuffer timeout), (out_ret, out_errno, nfds, readfds, writefds, exceptfds, timeout)) \
    AMS_SF_METHOD_INFO(C, H,  6, Result, Poll,       (sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::InAutoSelectBuffer fds, u32 nfds, s32 timeout), (out_ret, out_errno, fds, nfds, timeout)) \
    AMS_SF_METHOD_INFO(C, H,  8, Result, Recv,       (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags), (out_ret, out_errno, fd, buf, flags)) \
    AMS_SF_METHOD_INFO(C, H,  9, Result, RecvFrom,   (sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags, sf::OutAutoSelectBuffer addr), (out_ret, out_errno, out_addrlen, fd, buf, flags, addr)) \
    AMS_SF_METHOD_INFO(C, H, 10, Result, Send,       (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags), (out_ret, out_errno, fd, data, flags)) \
    AMS_SF_METHOD_INFO(C, H, 11, Result, SendTo,     (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags, sf::InAutoSelectBuffer addr), (out_ret, out_errno, fd, data, flags, addr)) \
    AMS_SF_METHOD_INFO(C, H, 12, Result, Accept,     (sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer addr), (out_ret, out_errno, out_addrlen, fd, addr)) \
    AMS_SF_METHOD_INFO(C, H, 13, Result, Bind,       (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr), (out_ret, out_errno, fd, addr)) \
    AMS_SF_METHOD_INFO(C, H, 14, Result, Connect,    (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr), (out_ret, out_errno, fd, addr)) \
    AMS_SF_METHOD_INFO(C, H, 17, Result, GetSockOpt, (sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_optlen, s32 fd, s32 level, s32 optname, sf::OutAutoSelectBuffer optval), (out_ret, out_errno, out_optlen, fd, level, optname, optval)) \
    AMS_SF_METHOD_INFO(C, H, 18, Result, Listen,     (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 backlog), (out_ret, out_errno, fd, backlog)) \
    AMS_SF_METHOD_INFO(C, H, 20, Result, Fcntl,      (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 cmd, s32 flags), (out_ret, out_errno, fd, cmd, flags)) \
    AMS_SF_METHOD_INFO(C, H, 21, Result, SetSockOpt, (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 level, s32 optname, sf::InAutoSelectBuffer optval), (out_ret, out_errno, fd, level, optname, optval)) \
    AMS_SF_METHOD_INFO(C, H, 22, Result, Shutdown,   (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 how), (out_ret, out_errno, fd, how)) \
    AMS_SF_METHOD_INFO(C, H, 26, Result, Close,      (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd), (out_ret, out_errno, fd))

AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::ldn, IBsdMitmInterface, AMS_BSD_MITM_INTERFACE_INFO, 0x4E553516)

namespace ams::mitm::ldn {

    class BsdMitmService : public sf::MitmServiceImplBase {
    private:
        enum class SocketType {
            Real,       // Normal socket, forward to real BSD
            Virtual     // RyuLDN virtual socket
        };

        struct SocketEntry {
            SocketType type;
            s32 real_fd;           // Real FD (for real sockets)
            void* virtual_socket;  // VirtualSocket* (for virtual sockets)
            u32 address_family;
            u32 socket_type;
            u32 protocol_type;
        };

        static constexpr size_t MaxSockets = 128;
        SocketEntry socket_map[MaxSockets];
        os::Mutex socket_map_mutex;

        static ryuldn::proxy::LdnProxy* s_proxy;

        SocketEntry* GetSocketEntry(s32 fd);
        bool IsRyuLdnVirtualIP(u32 ip);
        bool EnsureProxyAvailable(sf::Out<s32> out_ret, sf::Out<u32> out_errno, const char* context);
        ryuldn::proxy::LdnProxySocket* GetVirtualSocket(s32 fd);

    public:
        BsdMitmService(std::shared_ptr<::Service> &&s, const sm::MitmProcessInfo &c);
        ~BsdMitmService();

        static bool ShouldMitm(const sm::MitmProcessInfo &client_info);

        static void RegisterProxy(ryuldn::proxy::LdnProxy* proxy);
        static void UnregisterProxy();

        // BSD IPC commands
        Result Socket(sf::Out<s32> out_fd, u32 domain, u32 type, u32 protocol);
        Result Select(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 nfds, sf::InAutoSelectBuffer readfds, sf::InAutoSelectBuffer writefds, sf::InAutoSelectBuffer exceptfds, sf::InAutoSelectBuffer timeout);
        Result Poll(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::InAutoSelectBuffer fds, u32 nfds, s32 timeout);
        Result Recv(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags);
        Result RecvFrom(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags, sf::OutAutoSelectBuffer addr);
        Result Send(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags);
        Result SendTo(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags, sf::InAutoSelectBuffer addr);
        Result Accept(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer addr);
        Result Bind(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr);
        Result Connect(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr);
        Result GetSockOpt(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_optlen, s32 fd, s32 level, s32 optname, sf::OutAutoSelectBuffer optval);
        Result Listen(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 backlog);
        Result Fcntl(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 cmd, s32 flags);
        Result SetSockOpt(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 level, s32 optname, sf::InAutoSelectBuffer optval);
        Result Shutdown(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, s32 how);
        Result Close(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd);
    };

    static_assert(ams::mitm::ldn::IsIBsdMitmInterface<BsdMitmService>);
}
