#pragma once
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <type_traits>

namespace RenderVtable {
inline void** Table(void* object) { return *reinterpret_cast<void***>(object); }

inline std::mutex writes;

struct Slot {
    void** address = nullptr;
    void* original = nullptr;
    void* observer = nullptr;
    unsigned index = 0;
    void (*log)(const char*, ...) = nullptr;
    std::atomic<bool>* retired = nullptr;
    std::atomic<void*>* currentOriginal = nullptr;
    bool writable = false;

    void* Next() const { return currentOriginal ? currentOriginal->load() : original; }

    bool Exchange(void* expected, void* replacement, bool allowAlreadySet = false) const {
        if (writable) {
            const auto prior = InterlockedCompareExchangePointer(address, replacement, expected);
            return prior == expected || (allowAlreadySet && prior == replacement);
        }
        std::lock_guard<std::mutex> guard(writes);
        DWORD protection;
        if (!VirtualProtect(address, sizeof(*address), PAGE_READWRITE, &protection)) return false;
        const auto prior = InterlockedCompareExchangePointer(address, replacement, expected);
        DWORD ignored;
        VirtualProtect(address, sizeof(*address), protection, &ignored);
        return prior == expected || (allowAlreadySet && prior == replacement);
    }

    bool Restore() const { return Exchange(observer, Next(), true); }
};

struct Invocation;
inline thread_local const Invocation* active = nullptr;

struct Invocation {
    const Invocation* previous = active;
    const Slot& entry;
    const void* self;
    bool recovering = false;

    Invocation(const Slot& entry, const void* self) : entry(entry), self(self) { active = this; }
    ~Invocation() { active = previous; }

    const Invocation* EarlierCall() const {
        for (auto* call = previous; call; call = call->previous)
            if (&call->entry == &entry && call->self == self) return call;
        return nullptr;
    }
};

template<class Fn, auto Observe>
struct Observer;

template<class Result, class Object, class... Args, auto Observe>
struct Observer<Result (STDMETHODCALLTYPE*)(Object*, Args...), Observe> {
    template<auto& entries, std::size_t position>
    static Result STDMETHODCALLTYPE Call(Object* self, Args... args) {
        const auto& entry = entries[position];
        auto next = reinterpret_cast<Result (STDMETHODCALLTYPE*)(Object*, Args...)>(entry.Next());
        Invocation call(entry, self);
        if (const auto* earlier = call.EarlierCall()) {
            // Allow a runtime forwarding cycle to finish using the untouched interface entry.
            if (!earlier->recovering) {
                call.recovering = true;
                if (entry.retired) entry.retired->store(true);
                const bool restored = entry.Restore();
                if (entry.log) entry.log("RENDER_VTABLE_REENTRY slot=%u restored=%u", entry.index, unsigned(restored));
                if (restored) return next(self, args...);
            }
            if constexpr (!std::is_void_v<Result>) return E_FAIL;
            else return;
        }
        return Observe(next, self, args...);
    }
};
}
