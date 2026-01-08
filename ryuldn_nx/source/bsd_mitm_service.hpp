#pragma once
#include <switch.h>
#include <stratosphere.hpp>
#include "debug.hpp"
#include "ryuldn/ryuldn.hpp"

// BSD:u IPC command IDs
#define AMS_BSD_MITM_INTERFACE_INFO(C, H)                                                                  \
    AMS_SF_METHOD_INFO(C, H,  2, Result, Socket,     (sf::Out<s32> out_fd, u32 domain, u32 type, u32 protocol), (out_fd, domain, type, protocol)) \
    AMS_SF_METHOD_INFO(C, H,  8, Result, Recv,       (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags), (out_ret, out_errno, fd, buf, flags)) \
    AMS_SF_METHOD_INFO(C, H,  9, Result, RecvFrom,   (sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags, sf::OutAutoSelectBuffer addr), (out_ret, out_errno, out_addrlen, fd, buf, flags, addr)) \
    AMS_SF_METHOD_INFO(C, H, 10, Result, Send,       (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags), (out_ret, out_errno, fd, data, flags)) \
    AMS_SF_METHOD_INFO(C, H, 11, Result, SendTo,     (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags, sf::InAutoSelectBuffer addr), (out_ret, out_errno, fd, data, flags, addr)) \
    AMS_SF_METHOD_INFO(C, H, 13, Result, Bind,       (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr), (out_ret, out_errno, fd, addr)) \
    AMS_SF_METHOD_INFO(C, H, 14, Result, Connect,    (sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr), (out_ret, out_errno, fd, addr)) \
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
        };

        static constexpr size_t MaxSockets = 128;
        SocketEntry socket_map[MaxSockets];
        os::Mutex socket_map_mutex;

        static ryuldn::proxy::LdnProxy* s_proxy;

        SocketEntry* GetSocketEntry(s32 fd);
        bool IsRyuLdnVirtualIP(u32 ip);

    public:
        BsdMitmService(std::shared_ptr<::Service> &&s, const sm::MitmProcessInfo &c);
        ~BsdMitmService();

        static bool ShouldMitm(const sm::MitmProcessInfo &client_info);

        static void RegisterProxy(ryuldn::proxy::LdnProxy* proxy);
        static void UnregisterProxy();

        // BSD IPC commands
        Result Socket(sf::Out<s32> out_fd, u32 domain, u32 type, u32 protocol);
        Result Recv(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags);
        Result RecvFrom(sf::Out<s32> out_ret, sf::Out<u32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer buf, u32 flags, sf::OutAutoSelectBuffer addr);
        Result Send(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags);
        Result SendTo(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer data, u32 flags, sf::InAutoSelectBuffer addr);
        Result Bind(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr);
        Result Connect(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd, sf::InAutoSelectBuffer addr);
        Result Close(sf::Out<s32> out_ret, sf::Out<u32> out_errno, s32 fd);
    };

    static_assert(ams::mitm::ldn::IsIBsdMitmInterface<BsdMitmService>);
}
