#pragma once
// Shared Buffer Pool for RyuLDN Protocol
// Reduces memory fragmentation and allows buffer reuse
// Compatible with Ryujinx protocol - no behavioral changes

#include <stratosphere.hpp>
#include "types.hpp"

namespace ams::mitm::ldn::ryuldn {

    /**
     * Shared buffer pool for protocol packets
     * Reduces memory usage by allowing buffers to be borrowed and returned
     * Thread-safe for concurrent access
     */
    class BufferPool {
    private:
        struct BufferSlot {
            u8* buffer;
            bool inUse;
            BufferSlot* next;
        };

        static constexpr size_t BufferSize = MaxPacketSize;  // 128KB per buffer
        static constexpr size_t MaxBuffers = 3;  // Max concurrent buffer users
        
        BufferSlot _slots[MaxBuffers];
        BufferSlot* _freeList;
        os::Mutex _mutex;
        
        u8 _bufferStorage[MaxBuffers][BufferSize] alignas(64);  // Aligned for cache efficiency

    public:
        BufferPool();
        ~BufferPool() = default;

        /**
         * Borrow a buffer from the pool
         * Blocks if no buffers available (with timeout)
         * Returns nullptr on timeout
         */
        u8* BorrowBuffer(TimeSpan timeout = TimeSpan::FromSeconds(5));

        /**
         * Return a buffer to the pool
         * Must be called for every borrowed buffer
         */
        void ReturnBuffer(u8* buffer);

        /**
         * Get buffer size
         */
        static constexpr size_t GetBufferSize() { return BufferSize; }
    };

    /**
     * RAII wrapper for automatic buffer return
     * Ensures buffer is returned even if exception occurs
     */
    class ScopedBuffer {
    private:
        BufferPool* _pool;
        u8* _buffer;

    public:
        ScopedBuffer(BufferPool* pool, TimeSpan timeout = TimeSpan::FromSeconds(5))
            : _pool(pool), _buffer(nullptr) {
            if (_pool) {
                _buffer = _pool->BorrowBuffer(timeout);
            }
        }

        ~ScopedBuffer() {
            if (_pool && _buffer) {
                _pool->ReturnBuffer(_buffer);
            }
        }

        // No copy
        ScopedBuffer(const ScopedBuffer&) = delete;
        ScopedBuffer& operator=(const ScopedBuffer&) = delete;

        // Move support
        ScopedBuffer(ScopedBuffer&& other) noexcept
            : _pool(other._pool), _buffer(other._buffer) {
            other._buffer = nullptr;
        }

        u8* Get() const { return _buffer; }
        bool IsValid() const { return _buffer != nullptr; }
        operator bool() const { return IsValid(); }
        u8* operator->() const { return _buffer; }
    };

    /**
     * Global buffer pool instance
     * Initialized once at startup, shared across all components
     */
    extern BufferPool* g_sharedBufferPool;

    /**
     * Initialize the global buffer pool
     * Must be called before any RyuLDN components are created
     */
    Result InitializeBufferPool();

    /**
     * Finalize the global buffer pool
     * Must be called after all RyuLDN components are destroyed
     */
    void FinalizeBufferPool();

} // namespace ams::mitm::ldn::ryuldn
