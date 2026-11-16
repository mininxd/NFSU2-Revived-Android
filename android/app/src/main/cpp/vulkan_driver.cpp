#include "vulkan_driver.h"
#include "universal_gpu_loader.h"

void* nfsOpenVulkan(const char* hooks, const char* temp, const char* directory, const char* library) {
    int ok = universal_gpu_init(hooks, temp, directory, library);
    return ok ? universal_gpu_get_handle() : nullptr;
}

PFN_vkGetInstanceProcAddr nfsVulkanEntry(void* handle) {
    if (!handle) return nullptr;
    return universal_gpu_get_proc_addr();
}
