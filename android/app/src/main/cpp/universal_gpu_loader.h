#ifndef UNIVERSAL_GPU_LOADER_H
#define UNIVERSAL_GPU_LOADER_H

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>

struct UniversalGpuInfo {
    bool loaded = false;
    bool isCustom = false;
    std::string libraryName;
    std::string deviceName;
    std::string driverDescription;
    uint32_t apiVersion = VK_API_VERSION_1_3;
    uint32_t driverVersion = 0;
    uint32_t vendorId = 0;
    std::string errorMessage;
};

#ifdef __cplusplus
extern "C" {
#endif

/* C ABI functions for universal driver management */
int universal_gpu_init(const char *hooks, const char *temp, const char *dir, const char *lib);
void* universal_gpu_get_handle(void);
PFN_vkGetInstanceProcAddr universal_gpu_get_proc_addr(void);
uint32_t universal_gpu_get_api_version(void);
const char* universal_gpu_get_device_name(void);
int universal_gpu_is_custom(void);
const char* universal_gpu_get_error(void);
void universal_gpu_cleanup(void);

/* Universal vkGetInstanceProcAddr wrapper */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL universal_vkGetInstanceProcAddr(VkInstance instance, const char *pName);

#ifdef __cplusplus
}
#endif

#endif /* UNIVERSAL_GPU_LOADER_H */
