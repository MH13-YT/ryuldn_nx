#include <stratosphere.hpp>
#include "debug.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <switch.h>

// Définition de la variable atomique
namespace ams::log {
    std::atomic<u32> gLogLevel{3};  // INFO par défaut
}

namespace ams::log {
    
    // Buffer statique pour éviter les allocations dynamiques
    static constexpr size_t LOG_BUFFER_SIZE = 1024;
    static char g_logBuffer[LOG_BUFFER_SIZE];
    static ams::os::SdkMutex g_logMutex;
    
    void LogFormatImpl(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        
        std::scoped_lock lk(g_logMutex);
        
        // Format dans le buffer
        int len = std::vsnprintf(g_logBuffer, LOG_BUFFER_SIZE, fmt, args);
        va_end(args);
        
        if (len > 0) {
            // Sortie vers stdout (visible dans nxlink/console)
            std::fputs(g_logBuffer, stdout);
            std::fflush(stdout);
            
            // Sortie vers le debug SVC Nintendo Switch
            svcOutputDebugString(g_logBuffer, static_cast<u64>(len));
        }
    }
    
    void LogHexImpl(const void *data, int size) {
        if (!data || size <= 0) return;
        
        std::scoped_lock lk(g_logMutex);
        
        const u8 *bytes = static_cast<const u8*>(data);
        constexpr int BYTES_PER_LINE = 16;
        
        for (int i = 0; i < size; i += BYTES_PER_LINE) {
            // Offset
            std::printf("%08x: ", i);
            
            // Hex dump
            for (int j = 0; j < BYTES_PER_LINE; ++j) {
                if (i + j < size) {
                    std::printf("%02x ", bytes[i + j]);
                } else {
                    std::printf("   ");
                }
                
                // Séparateur au milieu
                if (j == 7) std::printf(" ");
            }
            
            // ASCII dump
            std::printf(" |");
            for (int j = 0; j < BYTES_PER_LINE && i + j < size; ++j) {
                u8 c = bytes[i + j];
                std::printf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            std::printf("|\n");
        }
        
        std::fflush(stdout);
    }
    
    Result Initialize() {
        // Initialiser la console pour les logs si nécessaire
        #ifdef DEBUG
        consoleInit(nullptr);
        #endif
        
        gLogLevel.store(3, std::memory_order_relaxed); // INFO par défaut
        LOG_INFO(COMP_MAIN, "Logging system initialized");
        
        return ResultSuccess();
    }
    
    void Finalize() {
        LOG_INFO(COMP_MAIN, "Logging system shutting down");
        
        #ifdef DEBUG
        consoleExit(nullptr);
        #endif
    }
    
    void LogHeapUsage([[maybe_unused]] const char* tag) {
        const u32 el = gLogLevel.load(std::memory_order_relaxed);
        if (el < 3) return;  // INFO+ seulement
        
        u64 heap_used = 0, heap_total = 0;
        
        #ifdef __SWITCH__
        // Récupère les vraies stats système
        Result rc = svcGetInfo(&heap_used, 0, CUR_PROCESS_HANDLE, 0);  // InfoType_UsedHeapMemorySize
        if (R_SUCCEEDED(rc)) {
            svcGetInfo(&heap_total, 1, CUR_PROCESS_HANDLE, 0);  // InfoType_TotalHeapMemorySize
        }
        #endif
        
        float percent = (heap_total > 0) ? (float(heap_used) * 100.0f / float(heap_total)) : 0.0f;
        
        LogFormatImpl("[%s] Heap: %7llu KB / %7llu KB (%.1f%%)\n", 
                    tag, heap_used / 1024, heap_total / 1024, percent);
    }
}
