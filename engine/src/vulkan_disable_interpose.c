// See worker_process.h / README.md "Known issues" for the full story:
// obscan_context_destroy() constructs Creality's SuperPoint feature-
// matching network (ncnn + Vulkan, via MoltenVK on macOS) purely to
// immediately tear it down again — it's never actually used. On this
// hardware that step has been observed to either crash fast (a known,
// separate libomp bug — harmless to device state, since it happens
// *after* the vendor SDK's own USB/RPC device-release steps) or hang
// indefinitely constructing a Vulkan device, which happens *before*
// those same release steps and is what leaves the physical scanner
// needing a power cycle to talk to again.
//
// This is built as a standalone dylib and loaded ONLY into the worker
// subprocess via DYLD_INSERT_LIBRARIES (see worker_process.cpp) — never
// linked into our own binaries, never touching anything in Creality's
// or Orbbec's own files. It interposes MoltenVK's vkCreateInstance and
// vkCreateDevice to fail immediately instead of attempting real GPU
// work. Any well-behaved Vulkan consumer, ncnn included, already has to
// handle "no usable Vulkan device" as a normal, expected outcome — real
// machines without a compatible GPU exist — so this should make the
// vendor SDK take its own existing fallback path (or simply skip
// SuperPoint) instead of the pathological one.
//
// macOS-only (DYLD_INSERT_LIBRARIES/__interpose is a Darwin mechanism).
// Built with -undefined dynamic_lookup: vkCreateInstance/vkCreateDevice
// aren't linked against anything here by design — they're resolved at
// runtime against whichever Vulkan loader (MoltenVK) is also present in
// the process by the time this binds.

#include <stdint.h>

typedef int32_t VkResult;
#define VK_ERROR_INITIALIZATION_FAILED (-3)

// We never dereference these — only vkCreateInstance/vkCreateDevice's
// calling convention (pointer-sized args, VkResult return) matters.
static VkResult FakeVkCreateInstance(const void* create_info, const void* allocator, void** out_instance) {
    (void)create_info;
    (void)allocator;
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
    if (out_device != 0) {
        *out_device = 0;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

extern VkResult vkCreateInstance(const void* pCreateInfo, const void* pAllocator, void** pInstance);
extern VkResult vkCreateDevice(void* physicalDevice, const void* pCreateInfo, const void* pAllocator,
                                void** pDevice);

typedef struct {
    const void* replacement;
    const void* replacee;
} DyldInterpose;

__attribute__((used)) static const DyldInterpose kInterposers[] __attribute__((section("__DATA,__interpose"))) = {
    {(const void*)FakeVkCreateInstance, (const void*)vkCreateInstance},
    {(const void*)FakeVkCreateDevice, (const void*)vkCreateDevice},
};
