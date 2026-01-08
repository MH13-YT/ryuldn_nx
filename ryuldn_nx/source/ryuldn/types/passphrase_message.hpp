#pragma once
#include <vapours.hpp>

namespace ams::mitm::ldn::ryuldn {

    // Passphrase message (128 bytes)
    // Matches Ryujinx LdnRyu/Types/PassphraseMessage.cs
    struct PassphraseMessage {
        char passphrase[0x80];  // Null-terminated passphrase string
    } __attribute__((packed));
    static_assert(sizeof(PassphraseMessage) == 0x80, "PassphraseMessage must be 0x80 bytes");

}
