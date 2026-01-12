#include "buffer_pool.hpp"
#include "../debug.hpp"

// Forward declarations for mitm allocator
namespace ams::mitm {
    void* Allocate(size_t size);
    void Deallocate(void* p, size_t size);
}

namespace ams::mitm::ldn::ryuldn {

    BufferPool* g_sharedBufferPool = nullptr;

    BufferPool::BufferPool()
        : _freeList(nullptr),
          _mutex(false)
    {
        // Initialize all slots
        for (size_t i = 0; i < MaxBuffers; i++) {
            _slots[i].buffer = _bufferStorage[i];
            _slots[i].inUse = false;
            _slots[i].next = (i < MaxBuffers - 1) ? &_slots[i + 1] : nullptr;
        }
        
        _freeList = &_slots[0];
        
        LOG_INFO_ARGS(COMP_RLDN_BUFPOOL, "BufferPool: Initialized with %zu buffers of %zu bytes each (total: %zu KB)",
                 MaxBuffers, BufferSize, (MaxBuffers * BufferSize) / 1024);
    }

    u8* BufferPool::BorrowBuffer(TimeSpan timeout) {
        auto startTime = os::GetSystemTick();
        
        while (true) {
            {
                std::scoped_lock lk(_mutex);
                
                if (_freeList != nullptr) {
                    BufferSlot* slot = _freeList;
                    _freeList = slot->next;
                    slot->inUse = true;
                    slot->next = nullptr;
                    
                    LOG_INFO_ARGS(COMP_RLDN_BUFPOOL, "BufferPool: Buffer borrowed (ptr=%p)", slot->buffer);
                    return slot->buffer;
                }
            }
            
            // Check timeout
            auto elapsed = os::ConvertToTimeSpan(os::GetSystemTick() - startTime);
            if (elapsed >= timeout) {
                LOG_INFO(COMP_RLDN_BUFPOOL, "BufferPool: Borrow timeout - no buffers available");
                return nullptr;
            }
            
            // Wait a bit before retrying
            os::SleepThread(TimeSpan::FromMilliSeconds(10));
        }
    }

    void BufferPool::ReturnBuffer(u8* buffer) {
        if (buffer == nullptr) {
            return;
        }
        
        std::scoped_lock lk(_mutex);
        
        // Find the slot for this buffer
        BufferSlot* slot = nullptr;
        for (size_t i = 0; i < MaxBuffers; i++) {
            if (_slots[i].buffer == buffer) {
                slot = &_slots[i];
                break;
            }
        }
        
        if (slot == nullptr) {
            LOG_INFO_ARGS(COMP_RLDN_BUFPOOL, "BufferPool: ERROR - Attempted to return invalid buffer (ptr=%p)", buffer);
            return;
        }
        
        if (!slot->inUse) {
            LOG_INFO_ARGS(COMP_RLDN_BUFPOOL, "BufferPool: WARNING - Buffer already returned (ptr=%p)", buffer);
            return;
        }
        
        // Return to free list
        slot->inUse = false;
        slot->next = _freeList;
        _freeList = slot;
        
        LOG_INFO_ARGS(COMP_RLDN_BUFPOOL, "BufferPool: Buffer returned (ptr=%p)", buffer);
    }

    Result InitializeBufferPool() {
        if (g_sharedBufferPool != nullptr) {
            return MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
        }
        
        // Allocate using custom heap (mitm::Allocate)
        void* memory = mitm::Allocate(sizeof(BufferPool));
        if (memory == nullptr) {
            LOG_INFO(COMP_RLDN_BUFPOOL, "BufferPool: Failed to allocate memory");
            return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        }
        
        // Placement new
        g_sharedBufferPool = new (memory) BufferPool();
        
        LOG_INFO(COMP_RLDN_BUFPOOL, "BufferPool: Global pool initialized");
        return ResultSuccess();
    }

    void FinalizeBufferPool() {
        if (g_sharedBufferPool != nullptr) {
            g_sharedBufferPool->~BufferPool();
            mitm::Deallocate(g_sharedBufferPool, sizeof(BufferPool));
            g_sharedBufferPool = nullptr;
            
            LOG_INFO(COMP_RLDN_BUFPOOL, "BufferPool: Global pool finalized");
        }
    }

} // namespace ams::mitm::ldn::ryuldn
