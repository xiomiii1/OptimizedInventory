#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

#include <android/log.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <vector>
#include <string>
#include <string_view>

namespace fastinventory {

namespace {
constexpr char kTag[] = "FastInventory";

// Minimal AArch64 signatures for the container UI path. They are resolved
// at runtime against libminecraftpe.so, so the mod does not depend on fixed
// virtual addresses.
constexpr std::string_view kContainerOpenPattern =
    "? ? ? A9 ? ? ? F9 FD 03 00 91 F3 03 00 AA ? ? ? 94 ? ? ? F9 E1 03 1F 2A ? ? ? 94";
constexpr std::string_view kContainerDtorPattern =
    "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 56 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F9 ? ? ? 90 ? ? ? 91 ? ? ? F9 ? ? ? F9 ? ? ? 91 ? ? ? F9 ? ? ? 94";
constexpr std::string_view kScreenViewRenderPattern =
    "? ? ? FC ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 48 D0 3B D5 FC 03 00 AA";
constexpr std::string_view kSlotSelectedPattern =
    "? ? ? A9 FD 03 00 91 ? ? ? F9 ? ? ? F9 00 01 3F D6 E0 03 1F 2A ? ? ? A8 C0 03 5F D6 ? ? ? D1";
constexpr std::string_view kGetItemStackPattern =
    "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? 91 54 D0 3B D5 F3 03 00 AA ? ? ? 91 ? ? ? F9 ? ? ? F8 ? ? ? 95 ? ? ? F9 ? ? ? F9 ? ? ? 91";

using Void8 = void (*)(void*, void*, void*, void*, void*, void*, void*, void*);
using ContainerOpenFn = Void8;
using ContainerDtorFn = Void8;
using ScreenViewRenderFn = Void8;
using ContainerSlotSelectedFn = std::uint32_t (*)(void*, const std::string&, int);
using ContainerGetItemStackFn = void* (*)(void*, const std::string&, int);

struct CacheEntry {
    void* controller = nullptr;
    int index = -1;
    std::string collection;
    void* stack = nullptr;
    std::uint64_t generation = 0;
    bool valid = false;
};

constexpr std::size_t kCacheSize = 96;
thread_local std::array<CacheEntry, kCacheSize> cache{};
thread_local std::size_t cacheCursor = 0;
thread_local std::uint64_t renderGeneration = 0;
std::atomic_bool installed{false};

ContainerOpenFn originalContainerOpen = nullptr;
ContainerDtorFn originalContainerDtor = nullptr;
ScreenViewRenderFn originalScreenViewRender = nullptr;
ContainerSlotSelectedFn originalSlotSelected = nullptr;
ContainerGetItemStackFn originalGetItemStack = nullptr;

std::array<std::uintptr_t, 5> installedTargets{};
std::size_t hookCount = 0;

void log(const char* message) {
    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", message);
}

void clearCache() {
    for (auto& entry : cache) {
        entry.valid = false;
        entry.controller = nullptr;
        entry.index = -1;
        entry.collection.clear();
        entry.stack = nullptr;
        entry.generation = 0;
    }
    cacheCursor = 0;
}

void beginRenderFrame() {
    ++renderGeneration;
    if (renderGeneration == 0) renderGeneration = 1;
    clearCache();
}

void* cachedGetItemStack(void* controller, const std::string& collection, int index) {
    if (!originalGetItemStack || !controller || index < 0) return nullptr;

    for (auto& entry : cache) {
        if (!entry.valid || entry.generation != renderGeneration) continue;
        if (entry.controller != controller || entry.index != index) continue;
        if (entry.collection != collection) continue;
        return entry.stack;
    }

    void* stack = originalGetItemStack(controller, collection, index);
    CacheEntry& entry = cache[cacheCursor++ % kCacheSize];
    entry.controller = controller;
    entry.index = index;
    entry.collection = collection;
    entry.stack = stack;
    entry.generation = renderGeneration;
    entry.valid = true;
    return stack;
}

void containerOpenHook(void* a0, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7) {
    clearCache();
    if (originalContainerOpen) originalContainerOpen(a0, a1, a2, a3, a4, a5, a6, a7);
}

void containerDtorHook(void* a0, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7) {
    clearCache();
    if (originalContainerDtor) originalContainerDtor(a0, a1, a2, a3, a4, a5, a6, a7);
}

void screenViewRenderHook(void* a0, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7) {
    beginRenderFrame();
    if (originalScreenViewRender) originalScreenViewRender(a0, a1, a2, a3, a4, a5, a6, a7);
}

std::uint32_t slotSelectedHook(void* self, const std::string& collection, int index) {
    clearCache();
    return originalSlotSelected ? originalSlotSelected(self, collection, index) : 0;
}

void* getItemStackHook(void* self, const std::string& collection, int index) {
    return cachedGetItemStack(self, collection, index);
}

std::uintptr_t resolveOne(std::string_view pattern, const char* library) {
    std::vector<std::string> patterns;
    patterns.emplace_back(pattern);
    const auto resolved = pl::memory::resolveSignatures(patterns, library);
    const auto it = resolved.find(patterns.front());
    return it == resolved.end() ? 0 : it->second;
}

bool install() {
    if (installed.load(std::memory_order_acquire)) return true;

    const char* library = "libminecraftpe.so";
    const std::uintptr_t open = resolveOne(kContainerOpenPattern, library);
    const std::uintptr_t dtor = resolveOne(kContainerDtorPattern, library);
    const std::uintptr_t render = resolveOne(kScreenViewRenderPattern, library);
    const std::uintptr_t selected = resolveOne(kSlotSelectedPattern, library);
    const std::uintptr_t getter = resolveOne(kGetItemStackPattern, library);

    if (!open || !dtor || !render || !selected || !getter) {
        log("Required inventory signatures were not resolved");
        return false;
    }

    hookCount = 0;
    auto installHook = [](std::uintptr_t address, void* detour, void** original) -> bool {
        if (!address) return false;
        return pl::memory::hook(reinterpret_cast<void*>(address), detour, original) == 0;
    };
    auto rollback = [&]() {
        const std::array<std::pair<std::uintptr_t, void*>, 5> current = {{
            {installedTargets[0], reinterpret_cast<void*>(containerOpenHook)},
            {installedTargets[1], reinterpret_cast<void*>(containerDtorHook)},
            {installedTargets[2], reinterpret_cast<void*>(screenViewRenderHook)},
            {installedTargets[3], reinterpret_cast<void*>(slotSelectedHook)},
            {installedTargets[4], reinterpret_cast<void*>(getItemStackHook)},
        }};
        for (std::size_t i = 0; i < hookCount; ++i) {
            if (current[i].first) pl::memory::unhook(reinterpret_cast<void*>(current[i].first), current[i].second);
            installedTargets[i] = 0;
        }
        hookCount = 0;
        originalContainerOpen = nullptr;
        originalContainerDtor = nullptr;
        originalScreenViewRender = nullptr;
        originalSlotSelected = nullptr;
        originalGetItemStack = nullptr;
    };

    if (installHook(open, reinterpret_cast<void*>(containerOpenHook), reinterpret_cast<void**>(&originalContainerOpen))) {
        installedTargets[hookCount++] = open;
    } else {
        log("Failed to hook ContainerScreenController::open");
        return false;
    }
    if (installHook(dtor, reinterpret_cast<void*>(containerDtorHook), reinterpret_cast<void**>(&originalContainerDtor))) {
        installedTargets[hookCount++] = dtor;
    } else {
        log("Failed to hook ContainerScreenController destructor");
        rollback();
        return false;
    }
    if (installHook(render, reinterpret_cast<void*>(screenViewRenderHook), reinterpret_cast<void**>(&originalScreenViewRender))) {
        installedTargets[hookCount++] = render;
    } else {
        log("Failed to hook ScreenView::render");
        rollback();
        return false;
    }
    if (installHook(selected, reinterpret_cast<void*>(slotSelectedHook), reinterpret_cast<void**>(&originalSlotSelected))) {
        installedTargets[hookCount++] = selected;
    } else {
        log("Failed to hook container slot selection");
        rollback();
        return false;
    }
    if (installHook(getter, reinterpret_cast<void*>(getItemStackHook), reinterpret_cast<void**>(&originalGetItemStack))) {
        installedTargets[hookCount++] = getter;
    } else {
        log("Failed to hook container item lookup");
        rollback();
        return false;
    }

    clearCache();
    installed.store(true, std::memory_order_release);
    log("Optimized inventory hooks installed");
    return true;
}

void uninstall() {
    const std::array<std::pair<std::uintptr_t, void*>, 5> installedHooks = {{
        {installedTargets[0], reinterpret_cast<void*>(containerOpenHook)},
        {installedTargets[1], reinterpret_cast<void*>(containerDtorHook)},
        {installedTargets[2], reinterpret_cast<void*>(screenViewRenderHook)},
        {installedTargets[3], reinterpret_cast<void*>(slotSelectedHook)},
        {installedTargets[4], reinterpret_cast<void*>(getItemStackHook)},
    }};
    for (std::size_t i = 0; i < hookCount; ++i) {
        if (installedHooks[i].first) {
            pl::memory::unhook(reinterpret_cast<void*>(installedHooks[i].first), installedHooks[i].second);
        }
        installedTargets[i] = 0;
    }
    hookCount = 0;
    originalContainerOpen = nullptr;
    originalContainerDtor = nullptr;
    originalScreenViewRender = nullptr;
    originalSlotSelected = nullptr;
    originalGetItemStack = nullptr;
    clearCache();
    installed.store(false, std::memory_order_release);
}

void* (*dlopenOriginal)(const char*, int) = nullptr;
std::uintptr_t dlopenTarget = 0;
thread_local bool resolving = false;

void* dlopenHookFn(const char* filename, int flags) {
    void* result = dlopenOriginal ? dlopenOriginal(filename, flags) : nullptr;
    if (result && filename && std::strstr(filename, "libminecraftpe.so") && !resolving) {
        resolving = true;
        install();
        resolving = false;
    }
    return result;
}

void watchMinecraftLibrary() {
    void* current = dlopen("libminecraftpe.so", RTLD_NOW | RTLD_NOLOAD);
    if (current) {
        dlclose(current);
        install();
        return;
    }

    void* dl = dlopen("libdl.so", RTLD_NOW | RTLD_NOLOAD);
    if (!dl) dl = dlopen("libdl.so", RTLD_NOW);
    if (!dl) return;

    auto symbol = reinterpret_cast<void*>(dlsym(dl, "dlopen"));
    if (symbol) {
        if (pl::memory::hook(symbol, reinterpret_cast<void*>(dlopenHookFn), reinterpret_cast<void**>(&dlopenOriginal)) == 0) {
            dlopenTarget = reinterpret_cast<std::uintptr_t>(symbol);
        }
    }
    dlclose(dl);
}

} // namespace

class FastInventoryMod {
public:
    static FastInventoryMod& instance() {
        static FastInventoryMod mod;
        return mod;
    }

    bool load(pl::mod::ModContext&) {
        watchMinecraftLibrary();
        return true;
    }

    bool enable(pl::mod::ModContext&) {
        install();
        return true;
    }

    bool disable(pl::mod::ModContext&) {
        return true;
    }

    bool unload(pl::mod::ModContext&) {
        uninstall();
        if (dlopenTarget) {
            pl::memory::unhook(reinterpret_cast<void*>(dlopenTarget), reinterpret_cast<void*>(dlopenHookFn));
            dlopenTarget = 0;
        }
        dlopenOriginal = nullptr;
        return true;
    }
};

} // namespace fastinventory

PL_REGISTER_MOD(fastinventory::FastInventoryMod, fastinventory::FastInventoryMod::instance())
