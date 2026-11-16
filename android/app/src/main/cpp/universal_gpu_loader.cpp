#include "universal_gpu_loader.h"
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <memory>
#include <android/log.h>

#define TAG "NFSU2-UniversalGPU"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#if defined(NFS_CUSTOM_DRIVERS) && defined(__aarch64__)
#include "android_linker_ns.h"
#include <adrenotools/driver.h>
#endif

namespace {

typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vk_icdGetPhysicalDeviceProcAddr)(VkInstance instance, const char* pName);

class UniversalGpuLoaderState {
public:
    UniversalGpuInfo info;
    void *driverHandle = nullptr;
    PFN_vkGetInstanceProcAddr rawGetProc = nullptr;
    PFN_vkGetInstanceProcAddr icdGetProc = nullptr;
    PFN_vk_icdGetPhysicalDeviceProcAddr icdGetPhysProc = nullptr;

    void cleanup() {
        if (driverHandle) {
            dlclose(driverHandle);
            driverHandle = nullptr;
        }
        rawGetProc = nullptr;
        icdGetProc = nullptr;
        icdGetPhysProc = nullptr;
        info = UniversalGpuInfo{};
    }

    bool init(const char *hooks, const char *temp, const char *dir, const char *lib) {
        (void)hooks;
        (void)temp;
        cleanup();

        // If no custom library specified, use system libvulkan.so
        if (!lib || !*lib || strcmp(lib, "system") == 0) {
            info.isCustom = false;
            driverHandle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
            if (!driverHandle) {
                info.errorMessage = "Failed to load system libvulkan.so";
                LOGE("%s", info.errorMessage.c_str());
                return false;
            }
            rawGetProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(driverHandle, "vkGetInstanceProcAddr"));
            info.loaded = (rawGetProc != nullptr);
            probeDeviceInfo();
            return info.loaded;
        }

        info.isCustom = true;
        info.libraryName = lib;

        std::string dirStr = dir ? dir : "";
        if (!dirStr.empty() && dirStr.back() != '/') dirStr += '/';
        std::string fullPath = dirStr + lib;

        LOGI("Initializing Universal GPU Driver: %s (path: %s)", lib, fullPath.c_str());

#if defined(NFS_CUSTOM_DRIVERS) && defined(__aarch64__)
        // Step 1: Direct loading using Linker Namespace Bypass
        // Gives Mali PanVK, Mesa, and other custom drivers access to Android vendor & HAL libraries.
        if (linkernsbypass_load_status()) {
            struct android_namespace_t *parentNs = nullptr;
            if (android_get_exported_namespace) {
                for (const char *nsName : {"sphal", "vendor", "system", "default"}) {
                    parentNs = android_get_exported_namespace(nsName);
                    if (parentNs) break;
                }
            }

            auto driverNs = android_create_namespace(
                lib, dirStr.c_str(), hooks, 2 /* ANDROID_NAMESPACE_TYPE_SHARED */, nullptr, parentNs);

            if (driverNs) {
                linkernsbypass_link_namespace_to_default_all_libs(driverNs);
                if (android_get_exported_namespace && android_link_namespaces_all_libs) {
                    for (const char *nsName : {"sphal", "vendor", "system", "vndk", "default"}) {
                        if (auto expNs = android_get_exported_namespace(nsName)) {
                            android_link_namespaces_all_libs(driverNs, expNs);
                        }
                    }
                }
                if (parentNs && android_link_namespaces_all_libs) {
                    android_link_namespaces_all_libs(driverNs, parentNs);
                }

                driverHandle = linkernsbypass_namespace_dlopen(lib, RTLD_NOW | RTLD_GLOBAL, driverNs);
                if (!driverHandle) {
                    driverHandle = linkernsbypass_namespace_dlopen(fullPath.c_str(), RTLD_NOW | RTLD_GLOBAL, driverNs);
                }
                if (driverHandle) {
                    LOGI("Successfully loaded universal driver %s via dynamic namespace!", lib);
                }
            }
        }
#endif

        // Step 2: Direct dlopen fallback
        if (!driverHandle) {
            driverHandle = dlopen(fullPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (driverHandle) {
                LOGI("Loaded universal driver %s via direct dlopen", lib);
            }
        }

#if defined(NFS_CUSTOM_DRIVERS) && defined(__aarch64__)
        // Step 3: Fallback through Adrenotools hook layer if present (for Qualcomm KGSL ioctls)
        if (!driverHandle && hooks && *hooks) {
            LOGI("Attempting Adrenotools fallback loader for %s", lib);
            driverHandle = adrenotools_open_libvulkan(
                RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM,
                temp && *temp ? temp : nullptr, hooks, dirStr.c_str(), lib, nullptr, nullptr);
        }
#endif

        if (!driverHandle) {
            info.errorMessage = "Could not open custom driver library " + std::string(lib) + ": " + std::string(dlerror() ? dlerror() : "unknown error");
            LOGE("%s", info.errorMessage.c_str());
            return false;
        }

        // Resolve entry points
        rawGetProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(driverHandle, "vkGetInstanceProcAddr"));
        icdGetProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(driverHandle, "vk_icdGetInstanceProcAddr"));
        icdGetPhysProc = reinterpret_cast<PFN_vk_icdGetPhysicalDeviceProcAddr>(dlsym(driverHandle, "vk_icdGetPhysicalDeviceProcAddr"));

        if (!rawGetProc && icdGetProc) {
            rawGetProc = icdGetProc;
        }

        typedef int (*PFN_icdNegotiate)(uint32_t *);
        auto negotiate = reinterpret_cast<PFN_icdNegotiate>(dlsym(driverHandle, "vk_icdNegotiateLoaderICDInterfaceVersion"));
        if (negotiate) {
            uint32_t ver = 5;
            negotiate(&ver);
            LOGI("Vulkan ICD interface version negotiated: %u", ver);
        }

        if (!rawGetProc && !icdGetProc) {
            info.errorMessage = "Driver " + std::string(lib) + " does not export vkGetInstanceProcAddr or vk_icdGetInstanceProcAddr";
            LOGE("%s", info.errorMessage.c_str());
            cleanup();
            return false;
        }

        info.loaded = true;
        probeDeviceInfo();

        // Enforce modern Vulkan 1.3 presentation for custom drivers (PanVK, Turnip, Mesa)
        if (info.apiVersion < VK_API_VERSION_1_3) {
            info.apiVersion = VK_API_VERSION_1_3;
        }

        LOGI("Universal GPU Driver Active: %s (API %u.%u.%u, Device: %s)",
             info.libraryName.c_str(),
             VK_VERSION_MAJOR(info.apiVersion),
             VK_VERSION_MINOR(info.apiVersion),
             VK_VERSION_PATCH(info.apiVersion),
             info.deviceName.c_str());

        return true;
    }

    void probeDeviceInfo() {
        auto pfnGet = rawGetProc ? rawGetProc : icdGetProc;
        if (!pfnGet) return;

        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "NFSU2 Driver Probe";
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &appInfo;

        VkInstance instance = VK_NULL_HANDLE;
        auto pfnCreate = reinterpret_cast<PFN_vkCreateInstance>(pfnGet(VK_NULL_HANDLE, "vkCreateInstance"));
        if (!pfnCreate && driverHandle) {
            pfnCreate = reinterpret_cast<PFN_vkCreateInstance>(dlsym(driverHandle, "vkCreateInstance"));
        }

        if (pfnCreate && pfnCreate(&ci, nullptr, &instance) == VK_SUCCESS) {
            auto pfnEnumPhys = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(dispatch(instance, "vkEnumeratePhysicalDevices"));
            auto pfnGetProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(dispatch(instance, "vkGetPhysicalDeviceProperties"));
            auto pfnDestroy = reinterpret_cast<PFN_vkDestroyInstance>(dispatch(instance, "vkDestroyInstance"));

            if (pfnEnumPhys && pfnGetProps) {
                uint32_t count = 0;
                if (pfnEnumPhys(instance, &count, nullptr) == VK_SUCCESS && count > 0) {
                    std::vector<VkPhysicalDevice> devs(count);
                    pfnEnumPhys(instance, &count, devs.data());
                    VkPhysicalDeviceProperties props{};
                    pfnGetProps(devs[0], &props);
                    info.deviceName = props.deviceName;
                    info.driverVersion = props.driverVersion;
                    info.vendorId = props.vendorID;
                    if (props.apiVersion >= VK_API_VERSION_1_0) {
                        info.apiVersion = props.apiVersion;
                    }
                }
            }
            if (pfnDestroy) pfnDestroy(instance, nullptr);
        }
    }

    PFN_vkVoidFunction dispatch(VkInstance instance, const char *pName) {
        if (!pName) return nullptr;
        if (rawGetProc) {
            auto fn = rawGetProc(instance, pName);
            if (fn) return fn;
        }
        if (icdGetProc) {
            auto fn = icdGetProc(instance, pName);
            if (fn) return fn;
        }
        if (driverHandle) {
            auto fn = reinterpret_cast<PFN_vkVoidFunction>(dlsym(driverHandle, pName));
            if (fn) return fn;
        }
        return nullptr;
    }
};

static UniversalGpuLoaderState s_loader;

} // namespace

extern "C" {

int universal_gpu_init(const char *hooks, const char *temp, const char *dir, const char *lib) {
    return s_loader.init(hooks, temp, dir, lib) ? 1 : 0;
}

void* universal_gpu_get_handle(void) {
    return s_loader.driverHandle;
}

PFN_vkGetInstanceProcAddr universal_gpu_get_proc_addr(void) {
    return universal_vkGetInstanceProcAddr;
}

uint32_t universal_gpu_get_api_version(void) {
    return s_loader.info.apiVersion;
}

const char* universal_gpu_get_device_name(void) {
    return s_loader.info.deviceName.c_str();
}

int universal_gpu_is_custom(void) {
    return s_loader.info.isCustom ? 1 : 0;
}

const char* universal_gpu_get_error(void) {
    return s_loader.info.errorMessage.c_str();
}

void universal_gpu_cleanup(void) {
    s_loader.cleanup();
}

VKAPI_ATTR VkResult VKAPI_CALL universal_vkEnumerateInstanceVersion(uint32_t *pApiVersion) {
    if (!pApiVersion) return VK_ERROR_INITIALIZATION_FAILED;
    *pApiVersion = s_loader.info.apiVersion >= VK_API_VERSION_1_3 ? s_loader.info.apiVersion : VK_API_VERSION_1_3;
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL universal_vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
    if (!pName) return nullptr;

    // Intercept instance-level queries to guarantee Vulkan 1.3 reporting
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(universal_vkGetInstanceProcAddr);
    }
    if (strcmp(pName, "vkEnumerateInstanceVersion") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(universal_vkEnumerateInstanceVersion);
    }

    return s_loader.dispatch(instance, pName);
}

} // extern "C"
