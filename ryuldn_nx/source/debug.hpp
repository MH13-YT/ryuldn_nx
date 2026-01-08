/*
 * Copyright (c) 2018 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include <stratosphere.hpp>
#include <stdio.h>
#include <cstdarg>

ams::Result SetLogging(u32 enabled);
ams::Result GetLogging(u32 *enabled);

namespace ams::log
{
    void LogFormatImpl(const char *fmt, ...);
    void LogHexImpl(const void *data, int size);
    Result Initialize();
    void Finalize();

#define LogFormat(fmt, ...) ams::log::LogFormatImpl(fmt "\n", ##__VA_ARGS__)
#define LogHex(data, size) ams::log::LogHexImpl(data, size)

// Enhanced logging macros for detailed diagnostics
#define LogError(fmt, ...) ams::log::LogFormatImpl("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LogWarning(fmt, ...) ams::log::LogFormatImpl("[WARNING] " fmt "\n", ##__VA_ARGS__)
#define LogInfo(fmt, ...) ams::log::LogFormatImpl("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LogDebug(fmt, ...) ams::log::LogFormatImpl("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define LogTrace(fmt, ...) ams::log::LogFormatImpl("[TRACE] " fmt "\n", ##__VA_ARGS__)

// Function entry/exit logging
#define LogFunctionEntry() ams::log::LogFormatImpl("[ENTER] %s\n", __PRETTY_FUNCTION__)
#define LogFunctionExit() ams::log::LogFormatImpl("[EXIT] %s\n", __PRETTY_FUNCTION__)

// Result logging helper
#define LogResult(res, desc) ams::log::LogFormatImpl("[RESULT] %s: 0x%x (%s)\n", desc, (res).GetValue(), R_SUCCEEDED(res) ? "SUCCESS" : "FAILED")

// Memory operation logging
#define LogMemAlloc(ptr, size) ams::log::LogFormatImpl("[MEM] Allocated %zu bytes at %p\n", (size_t)(size), (void*)(ptr))
#define LogMemFree(ptr) ams::log::LogFormatImpl("[MEM] Freed memory at %p\n", (void*)(ptr))

// Network operation logging
#define LogNetSocket(fd, domain, type, protocol) ams::log::LogFormatImpl("[NET] Socket created: fd=%d domain=%d type=%d protocol=%d\n", (fd), (domain), (type), (protocol))
#define LogNetConnect(fd, addr, port) ams::log::LogFormatImpl("[NET] Connect: fd=%d addr=%s port=%d\n", (fd), (addr), (port))
#define LogNetBind(fd, port) ams::log::LogFormatImpl("[NET] Bind: fd=%d port=%d\n", (fd), (port))
#define LogNetSend(fd, size) ams::log::LogFormatImpl("[NET] Send: fd=%d size=%d\n", (fd), (size))
#define LogNetRecv(fd, size) ams::log::LogFormatImpl("[NET] Recv: fd=%d size=%d\n", (fd), (size))
#define LogNetClose(fd) ams::log::LogFormatImpl("[NET] Close: fd=%d\n", (fd))
#define LogNetError(fd, errcode, desc) ams::log::LogFormatImpl("[NET ERROR] fd=%d errno=%d desc=%s\n", (fd), (errcode), (desc))

// Thread/synchronization logging
#define LogThreadCreate(name, priority) ams::log::LogFormatImpl("[THREAD] Created: %s priority=%d\n", (name), (priority))
#define LogThreadStart(name) ams::log::LogFormatImpl("[THREAD] Started: %s\n", (name))
#define LogThreadExit(name) ams::log::LogFormatImpl("[THREAD] Exited: %s\n", (name))
#define LogMutexLock(name) ams::log::LogFormatImpl("[MUTEX] Locked: %s\n", (name))
#define LogMutexUnlock(name) ams::log::LogFormatImpl("[MUTEX] Unlocked: %s\n", (name))
#define LogEventSignal(name) ams::log::LogFormatImpl("[EVENT] Signaled: %s\n", (name))
#define LogEventWait(name, timeout) ams::log::LogFormatImpl("[EVENT] Waiting: %s timeout=%llu\n", (name), (u64)(timeout))

// State transition logging
#define LogStateChange(from, to) ams::log::LogFormatImpl("[STATE] Transition: %s -> %s\n", (from), (to))
}
