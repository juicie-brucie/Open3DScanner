// See worker_process.h / README.md "Known issues" for the full story:
// obscan_context_destroy() constructs Creality's SuperPoint feature-
// matching network (ncnn + Vulkan, via MoltenVK on macOS) purely to
// immediately tear it down again — it's never actually used. On this
// hardware that step has been observed to either crash fast (a known,
// separate libomp bug) or hang indefinitely right after MoltenVK creates
// a VkDevice.
//
// This is built as a standalone dylib and loaded ONLY into the worker
// subprocess via DYLD_INSERT_LIBRARIES (see live_capture_test/main.cpp) —
// never linked into our own binaries, never touching anything in
// Creality's or Orbbec's own files. It makes Vulkan instance/device
// creation fail immediately instead of attempting real GPU work. Any
// well-behaved Vulkan consumer, ncnn included, already has to handle "no
// usable Vulkan device" as a normal outcome, so the vendor SDK should take
// its own fallback path (or skip SuperPoint) instead of the pathological
// one.
//
// Why interposing vkCreateInstance/vkCreateDevice alone was not enough
// (logs/stop_abi_fix_validation/live_capture_test_run*.log show MoltenVK
// still printing "Created VkInstance" and "Created VkDevice" with the
// earlier version of this file loaded): dyld's __interpose only rewrites
// symbol references bound through dyld. ncnn does not call those
// functions that way — it dlopen()s the Vulkan loader/MoltenVK, takes
// vkGetInstanceProcAddr via dlsym(), and fetches every other entry point
// through vkGetInstanceProcAddr. Neither route goes through the
// interposed binds. So this file now also interposes dlsym() and
// vkGetInstanceProcAddr()/vk_icdGetInstanceProcAddr() and hands back the
// failing versions from there as well. Calls made from *this* image are
// never interposed by dyld, which is what lets the wrappers below reach
// the real dlsym/vkGetInstanceProcAddr.
//
// Every interception is reported on stderr with a
// "[vulkan_disable_interpose]" prefix, so a run's log shows directly
// whether this fired — the absence of those lines next to a MoltenVK
// "Created VkInstance" line means it still isn't catching the path in use.
//
// macOS-only (DYLD_INSERT_LIBRARIES/__interpose is a Darwin mechanism).
// Built with -undefined dynamic_lookup: the vk* symbols aren't linked
// against anything here by design — they're resolved at runtime against
// the MoltenVK image inserted ahead of this one.

#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef int32_t VkResult;
#define VK_ERROR_INITIALIZATION_FAILED (-3)

typedef void (*PFN_vkVoidFunction)(void);

static void Note(const char* msg) {
    // write(2) rather than stdio: safe from any thread and unaffected by
    // stdout buffering in the worker.
    static const char kPrefix[] = "[vulkan_disable_interpose] ";
    (void)!write(STDERR_FILENO, kPrefix, sizeof(kPrefix) - 1);
    (void)!write(STDERR_FILENO, msg, strlen(msg));
    (void)!write(STDERR_FILENO, "\n", 1);
}

__attribute__((constructor)) static void OnLoad(void) {
    Note("loaded");
}

// We never dereference these — only the calling convention (pointer-sized
// args, VkResult return) matters.
static VkResult FakeVkCreateInstance(const void* create_info, const void* allocator, void** out_instance) {
    (void)create_info;
    (void)allocator;
    Note("blocked vkCreateInstance");
    if (out_instance != 0) {
        *out_instance = 0;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

static VkResult FakeVkCreateDevice(void* physical_device, const void* create_info, const void* allocator,
                                    void** out_device) {
    (void)physical_device;
    (void)create_info;
    (void)allocator;
    Note("blocked vkCreateDevice");
    if (out_device != 0) {
        *out_device = 0;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

extern VkResult vkCreateInstance(const void* pCreateInfo, const void* pAllocator, void** pInstance);
extern VkResult vkCreateDevice(void* physicalDevice, const void* pCreateInfo, const void* pAllocator,
                                void** pDevice);
extern PFN_vkVoidFunction vkGetInstanceProcAddr(void* instance, const char* pName);
// MoltenVK's ICD entry point; weak so this still loads against a MoltenVK
// build that doesn't export it.
extern PFN_vkVoidFunction vk_icdGetInstanceProcAddr(void* instance, const char* pName) __attribute__((weak_import));

// Returns the failing replacement for a create function name, or NULL.
static PFN_vkVoidFunction BlockedEntryPoint(const char* name) {
    if (name == 0) {
        return 0;
    }
    if (strcmp(name, "vkCreateInstance") == 0) {
        return (PFN_vkVoidFunction)FakeVkCreateInstance;
    }
    if (strcmp(name, "vkCreateDevice") == 0) {
        return (PFN_vkVoidFunction)FakeVkCreateDevice;
    }
    return 0;
}

static PFN_vkVoidFunction FakeVkGetInstanceProcAddr(void* instance, const char* name) {
    PFN_vkVoidFunction blocked = BlockedEntryPoint(name);
    if (blocked != 0) {
        Note("intercepted vkGetInstanceProcAddr lookup of a create function");
        return blocked;
    }
    return vkGetInstanceProcAddr(instance, name);
}

static PFN_vkVoidFunction FakeVkIcdGetInstanceProcAddr(void* instance, const char* name) {
    PFN_vkVoidFunction blocked = BlockedEntryPoint(name);
    if (blocked != 0) {
        Note("intercepted vk_icdGetInstanceProcAddr lookup of a create function");
        return blocked;
    }
    return vk_icdGetInstanceProcAddr != 0 ? vk_icdGetInstanceProcAddr(instance, name) : 0;
}

// Caveat: forwarding through our own dlsym call means RTLD_NEXT/RTLD_SELF
// lookups are resolved relative to this image instead of the original
// caller. This image is inserted ahead of everything except MoltenVK, so
// in practice the search order is nearly the same; it's only loaded into
// the throwaway capture worker, never our own long-lived processes.
static void* FakeDlsym(void* handle, const char* symbol) {
    if (symbol != 0) {
        if (strcmp(symbol, "vkGetInstanceProcAddr") == 0) {
            Note("intercepted dlsym(vkGetInstanceProcAddr)");
            return (void*)FakeVkGetInstanceProcAddr;
        }
        if (strcmp(symbol, "vk_icdGetInstanceProcAddr") == 0) {
            Note("intercepted dlsym(vk_icdGetInstanceProcAddr)");
            return (void*)FakeVkIcdGetInstanceProcAddr;
        }
        PFN_vkVoidFunction blocked = BlockedEntryPoint(symbol);
        if (blocked != 0) {
            Note("intercepted dlsym of a create function");
            return (void*)blocked;
        }
    }
    return dlsym(handle, symbol);
}

typedef struct {
    const void* replacement;
    const void* replacee;
} DyldInterpose;

__attribute__((used)) static const DyldInterpose kInterposers[] __attribute__((section("__DATA,__interpose"))) = {
    {(const void*)FakeVkCreateInstance, (const void*)vkCreateInstance},
    {(const void*)FakeVkCreateDevice, (const void*)vkCreateDevice},
    {(const void*)FakeVkGetInstanceProcAddr, (const void*)vkGetInstanceProcAddr},
    {(const void*)FakeDlsym, (const void*)dlsym},
    // vk_icdGetInstanceProcAddr is deliberately not in this table: it's a
    // weak import that may be NULL, and an interpose entry with a NULL
    // replacee is not something to rely on. It's caught via FakeDlsym
    // above, which is how ICD entry points are looked up anyway.
};
