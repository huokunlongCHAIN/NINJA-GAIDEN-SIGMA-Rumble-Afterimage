#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include <share.h>
#include <vector>

namespace {
class Handle {
    HANDLE value;
public:
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (IsValid()) CloseHandle(value); }
    bool IsValid() const { return value && value != INVALID_HANDLE_VALUE; }
    HANDLE Get() const { return value; }
};

struct Module {
    DWORD64 base;
    DWORD size;
    wchar_t name[MAX_MODULE_NAME32 + 1];
};

std::vector<Module> ReadModules(DWORD pid) {
    std::vector<Module> modules;
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snapshot.IsValid()) return modules;
    MODULEENTRY32W next{};
    next.dwSize = sizeof(next);
    for (BOOL ok = Module32FirstW(snapshot.Get(), &next); ok; ok = Module32NextW(snapshot.Get(), &next)) {
        Module module{reinterpret_cast<DWORD64>(next.modBaseAddr), next.modBaseSize, {}};
        wcscpy_s(module.name, next.szModule);
        modules.push_back(module);
    }
    return modules;
}

bool LogAddress(FILE* log, const std::vector<Module>& modules, const char* label, DWORD64 address) {
    for (const auto& module : modules) {
        if (address >= module.base && address - module.base < module.size) {
            std::fprintf(log, "%s %ls+0x%llX\n", label, module.name, address - module.base);
            return true;
        }
    }
    return false;
}

void LogStackCandidates(FILE* log, HANDLE process, const std::vector<Module>& modules, DWORD64 stack) {
    // The exhausted stack may lack usable unwind metadata for the faulting frame.
    // Record executable module addresses only and label them separately from frames.
    DWORD64 slots[256]{};
    SIZE_T size = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void*>(stack), slots, sizeof(slots), &size)) return;
    unsigned written = 0;
    DWORD64 last = 0;
    for (unsigned i = 0; i < size / sizeof(slots[0]) && written < 24; ++i) {
        if (!slots[i] || slots[i] == last) continue;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQueryEx(process, reinterpret_cast<const void*>(slots[i]), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
            !(info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            continue;
        if (!LogAddress(log, modules, "STACK_CANDIDATE", slots[i])) continue;
        last = slots[i];
        ++written;
    }
}

template<class Fn>
Fn Resolve(HMODULE library, const char* name) {
    return reinterpret_cast<Fn>(GetProcAddress(library, name));
}

void RecordException(FILE* log, HANDLE process, const DEBUG_EVENT& event) {
    const auto& exception = event.u.Exception;
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    std::fprintf(log, "%04u-%02u-%02uT%02u:%02u:%02uZ EXCEPTION pid=%lu thread=%lu code=0x%08lX first_chance=%lu\n",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, event.dwProcessId,
        event.dwThreadId, exception.ExceptionRecord.ExceptionCode, exception.dwFirstChance);
    const auto modules = ReadModules(event.dwProcessId);
    if (!LogAddress(log, modules, "FAULT", reinterpret_cast<DWORD64>(exception.ExceptionRecord.ExceptionAddress)))
        std::fputs("FAULT <unknown module>\n", log);
    std::fflush(log);

    Handle thread(OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, event.dwThreadId));
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (!thread.IsValid() || !GetThreadContext(thread.Get(), &context)) {
        std::fprintf(log, "STACK_CONTEXT_UNAVAILABLE error=%lu\n", GetLastError());
        return;
    }
    const DWORD64 stack = context.Rsp;
    unsigned frames = 0;
    HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dbghelp) {
        std::fprintf(log, "STACK_DBGHELP_UNAVAILABLE error=%lu\n", GetLastError());
        LogStackCandidates(log, process, modules, stack);
        return;
    }
    const auto initialize = Resolve<decltype(&SymInitializeW)>(dbghelp, "SymInitializeW");
    const auto cleanup = Resolve<decltype(&SymCleanup)>(dbghelp, "SymCleanup");
    const auto setOptions = Resolve<decltype(&SymSetOptions)>(dbghelp, "SymSetOptions");
    const auto walk = Resolve<decltype(&StackWalk64)>(dbghelp, "StackWalk64");
    const auto table = Resolve<decltype(&SymFunctionTableAccess64)>(dbghelp, "SymFunctionTableAccess64");
    const auto base = Resolve<decltype(&SymGetModuleBase64)>(dbghelp, "SymGetModuleBase64");
    if (initialize && cleanup && setOptions && walk && table && base) {
        // Only module-relative offsets are exported. Ignore external symbol paths.
        setOptions(SYMOPT_IGNORE_NT_SYMPATH | SYMOPT_NO_PROMPTS | SYMOPT_DEFERRED_LOADS |
            SYMOPT_DISABLE_SYMSRV_AUTODETECT);
        if (initialize(process, L".", TRUE)) {
            STACKFRAME64 frame{};
            frame.AddrPC = {context.Rip, 0, AddrModeFlat};
            frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
            frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
            for (unsigned i = 0; i < 32; ++i) {
                if (!frame.AddrPC.Offset) break;
                if (!LogAddress(log, modules, "FRAME", frame.AddrPC.Offset))
                    std::fputs("FRAME <unknown module>\n", log);
                ++frames;
                const auto previousPC = frame.AddrPC.Offset;
                const auto previousSP = frame.AddrStack.Offset;
                if (!walk(IMAGE_FILE_MACHINE_AMD64, process, thread.Get(), &frame, &context, nullptr, table, base, nullptr) ||
                    (frame.AddrPC.Offset == previousPC && frame.AddrStack.Offset == previousSP)) break;
            }
            cleanup(process);
        } else {
            std::fprintf(log, "STACK_INITIALIZE_FAILED error=%lu\n", GetLastError());
        }
    } else {
        std::fputs("STACK_API_UNAVAILABLE\n", log);
    }
    FreeLibrary(dbghelp);
    if (frames < 2) {
        std::fputs("STACK_UNWIND_INCOMPLETE\n", log);
        LogStackCandidates(log, process, modules, stack);
    }
    std::fflush(log);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    wchar_t* end = nullptr;
    const DWORD pid = std::wcstoul(argv[1], &end, 10);
    if (!pid || !end || *end) return 2;
    FILE* log = _wfsopen(argv[2], L"wb", _SH_DENYWR);
    if (!log) return 3;
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!process.IsValid() || !DebugActiveProcess(pid)) {
        std::fprintf(log, "TRACE_ATTACH_FAILED pid=%lu error=%lu\n", pid, GetLastError());
        std::fclose(log);
        return 4;
    }
    if (!DebugSetProcessKillOnExit(FALSE)) {
        std::fprintf(log, "TRACE_SETUP_FAILED error=%lu\n", GetLastError());
        DebugActiveProcessStop(pid);
        std::fclose(log);
        return 4;
    }
    std::fprintf(log, "TRACE_ATTACHED pid=%lu\n", pid);
    std::fflush(log);
    bool initialBreakpoint = true;
    bool stackOverflowSeen = false;
    DEBUG_EVENT event{};
    while (WaitForDebugEvent(&event, INFINITE)) {
        DWORD disposition = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            const DWORD code = event.u.Exception.ExceptionRecord.ExceptionCode;
            if (initialBreakpoint && code == EXCEPTION_BREAKPOINT && event.u.Exception.dwFirstChance) {
                initialBreakpoint = false;
            } else {
                if (code == EXCEPTION_STACK_OVERFLOW ? !stackOverflowSeen : !event.u.Exception.dwFirstChance) {
                    try {
                        RecordException(log, process.Get(), event);
                    } catch (...) {
                        std::fputs("STACK_RECORD_FAILED\n", log);
                        std::fflush(log);
                    }
                    stackOverflowSeen |= code == EXCEPTION_STACK_OVERFLOW;
                }
                disposition = DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            std::fprintf(log, "TRACE_EXIT pid=%lu code=0x%08lX\n", pid, event.u.ExitProcess.dwExitCode);
            std::fflush(log);
            break;
        }
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition)) break;
        if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            std::fclose(log);
            return 0;
        }
    }
    std::fprintf(log, "TRACE_INTERRUPTED error=%lu\n", GetLastError());
    DebugActiveProcessStop(pid);
    std::fclose(log);
    return 5;
}
