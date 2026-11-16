#define _GNU_SOURCE
/* roam_powervr.c -- PowerVR Fault Guard and Vulkan pipeline stability interceptor */
#include "roam_powervr.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
#include <dlfcn.h>
#include <android/log.h>

typedef void* VkInstance_t;
typedef void* VkDevice_t;
typedef void* VkPhysicalDevice_t;
typedef uint64_t VkPipelineCache_t;
typedef uint64_t VkPipeline_t;
typedef void (*PFN_vkVoidFunction_t)(void);
typedef PFN_vkVoidFunction_t (*PFN_vkGetInstanceProcAddr_t)(VkInstance_t instance, const char* pName);
typedef PFN_vkVoidFunction_t (*PFN_vkGetDeviceProcAddr_t)(VkDevice_t device, const char* pName);
typedef int32_t VkResult_t;

typedef struct {
    uint32_t robustBufferAccess;
    uint32_t fullDrawIndexUint32;
    uint32_t imageCubeArray;
    uint32_t independentBlend;
    uint32_t geometryShader;
    uint32_t tessellationShader;
    uint32_t sampleRateShading;
    uint32_t dualSrcBlend;
    uint32_t logicOp;
    uint32_t multiDrawIndirect;
    uint32_t drawIndirectFirstInstance;
    uint32_t depthClamp;
    uint32_t depthBiasClamp;
    uint32_t fillModeNonSolid;
    uint32_t depthBounds;
    uint32_t wideLines;
    uint32_t largePoints;
    uint32_t alphaToOne;
    uint32_t multiViewport;
    uint32_t samplerAnisotropy;
    uint32_t textureCompressionETC2;
    uint32_t textureCompressionASTC_LDR;
    uint32_t textureCompressionBC;
    uint32_t occlusionQueryPrecise;
    uint32_t pipelineStatisticsQuery;
    uint32_t vertexPipelineStoresAndAtomics;
    uint32_t fragmentStoresAndAtomics;
    uint32_t shaderTessellationAndGeometryPointSize;
    uint32_t shaderImageGatherExtended;
    uint32_t shaderStorageImageExtendedFormats;
    uint32_t shaderStorageImageMultisample;
    uint32_t shaderStorageImageReadWithoutFormat;
    uint32_t shaderStorageImageWriteWithoutFormat;
    uint32_t shaderUniformBufferArrayDynamicIndexing;
    uint32_t shaderSampledImageArrayDynamicIndexing;
    uint32_t shaderStorageBufferArrayDynamicIndexing;
    uint32_t shaderStorageImageArrayDynamicIndexing;
    uint32_t shaderClipDistance;
    uint32_t shaderCullDistance;
    uint32_t shaderFloat64;
    uint32_t shaderInt64;
    uint32_t shaderInt16;
    uint32_t shaderResourceResidency;
    uint32_t shaderResourceMinLod;
    uint32_t sparseBinding;
    uint32_t sparseResidencyBuffer;
    uint32_t sparseResidencyImage2D;
    uint32_t sparseResidencyImage3D;
    uint32_t sparseResidency2Samples;
    uint32_t sparseResidency4Samples;
    uint32_t sparseResidency8Samples;
    uint32_t sparseResidency16Samples;
    uint32_t sparseResidencyAliased;
    uint32_t variableMultisampleRate;
    uint32_t inheritedQueries;
} VkPhysicalDeviceFeatures_compat_t;

typedef struct {
    uint32_t sType;
    const void* pNext;
    uint32_t flags;
    uint32_t queueCreateInfoCount;
    const void* pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
    const VkPhysicalDeviceFeatures_compat_t* pEnabledFeatures;
} VkDeviceCreateInfo_compat_t;

typedef VkResult_t (*PFN_vkCreateDevice_t)(
    VkPhysicalDevice_t physicalDevice,
    const VkDeviceCreateInfo_compat_t* pCreateInfo,
    const void* pAllocator,
    VkDevice_t* pDevice);

typedef void (*PFN_vkGetPhysicalDeviceFeatures_t)(
    VkPhysicalDevice_t physicalDevice,
    VkPhysicalDeviceFeatures_compat_t* pFeatures);

typedef VkResult_t (*PFN_vkCreateGraphicsPipelines_t)(
    VkDevice_t device,
    VkPipelineCache_t pipelineCache,
    uint32_t createInfoCount,
    const void* pCreateInfos,
    const void* pAllocator,
    VkPipeline_t* pPipelines);

typedef struct {
    uint32_t sType;
    void* pNext;
    VkPhysicalDeviceFeatures_compat_t features;
} VkPhysicalDeviceFeatures2_compat_t;

typedef struct {
    uint32_t sType;
    void* pNext;
    uint32_t robustBufferAccess2;
    uint32_t robustImageAccess2;
    uint32_t nullDescriptor;
} VkPhysicalDeviceRobustness2FeaturesEXT_compat_t;

typedef struct {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    uint32_t limits[128];
    uint32_t sparseProperties[5];
} VkPhysicalDeviceProperties_compat_t;

typedef struct {
    char extensionName[256];
    uint32_t specVersion;
} VkExtensionProperties_compat_t;

typedef void (*PFN_vkGetPhysicalDeviceProperties_t)(
    VkPhysicalDevice_t physicalDevice,
    VkPhysicalDeviceProperties_compat_t* pProperties);

typedef void (*PFN_vkGetPhysicalDeviceFeatures2_t)(
    VkPhysicalDevice_t physicalDevice,
    VkPhysicalDeviceFeatures2_compat_t* pFeatures);

typedef VkResult_t (*PFN_vkEnumerateDeviceExtensionProperties_t)(
    VkPhysicalDevice_t physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties_compat_t* pProperties);

static PFN_vkGetInstanceProcAddr_t s_real_vkGetInstanceProcAddr = NULL;
static PFN_vkGetDeviceProcAddr_t s_real_vkGetDeviceProcAddr = NULL;
static PFN_vkCreateDevice_t s_real_vkCreateDevice = NULL;
static PFN_vkGetPhysicalDeviceFeatures_t s_real_vkGetPhysicalDeviceFeatures = NULL;
static PFN_vkGetPhysicalDeviceProperties_t s_real_vkGetPhysicalDeviceProperties = NULL;
static PFN_vkGetPhysicalDeviceFeatures2_t s_real_vkGetPhysicalDeviceFeatures2 = NULL;
static PFN_vkEnumerateDeviceExtensionProperties_t s_real_vkEnumerateDeviceExtensionProperties = NULL;
static PFN_vkCreateGraphicsPipelines_t s_real_vkCreateGraphicsPipelines = NULL;

static VkResult_t guarded_vkCreateDevice(
    VkPhysicalDevice_t physicalDevice,
    const VkDeviceCreateInfo_compat_t* pCreateInfo,
    const void* pAllocator,
    VkDevice_t* pDevice) {
    if (!s_real_vkCreateDevice) return -3; /* VK_ERROR_INITIALIZATION_FAILED */
    if (!pCreateInfo) return s_real_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);

    VkDeviceCreateInfo_compat_t safeInfo = *pCreateInfo;
    VkPhysicalDeviceFeatures_compat_t safeFeatures;

    if (pCreateInfo->pEnabledFeatures) {
        memcpy(&safeFeatures, pCreateInfo->pEnabledFeatures, sizeof(safeFeatures));
        VkPhysicalDeviceFeatures_compat_t supported;
        memset(&supported, 0, sizeof(supported));

        if (s_real_vkGetPhysicalDeviceFeatures) {
            s_real_vkGetPhysicalDeviceFeatures(physicalDevice, &supported);
        }

        if (!supported.occlusionQueryPrecise && safeFeatures.occlusionQueryPrecise) {
            __android_log_print(ANDROID_LOG_INFO, "NFSU2",
                "PowerVR Guard: stripped unsupported occlusionQueryPrecise from vkCreateDevice (PowerVR / IMG BXM fix)");
            safeFeatures.occlusionQueryPrecise = 0;
        }
        if (!supported.sampleRateShading && safeFeatures.sampleRateShading) {
            __android_log_print(ANDROID_LOG_INFO, "NFSU2",
                "PowerVR Guard: stripped unsupported sampleRateShading from vkCreateDevice");
            safeFeatures.sampleRateShading = 0;
        }
        if (!supported.geometryShader && safeFeatures.geometryShader) {
            safeFeatures.geometryShader = 0;
        }
        if (!supported.multiViewport && safeFeatures.multiViewport) {
            safeFeatures.multiViewport = 0;
        }
        if (supported.robustBufferAccess) {
            safeFeatures.robustBufferAccess = 1;
        } else {
            safeFeatures.robustBufferAccess = 0;
        }
        safeInfo.pEnabledFeatures = &safeFeatures;
    }

    // Detect GPU vendor & properties
    VkPhysicalDeviceProperties_compat_t props;
    memset(&props, 0, sizeof(props));
    if (s_real_vkGetPhysicalDeviceProperties) {
        s_real_vkGetPhysicalDeviceProperties(physicalDevice, &props);
    }
    int is_adreno = (props.vendorID == 0x5143);

    // Enumerate supported extensions
    uint32_t ext_count = 0;
    int has_robustness2 = 0;
    int has_qcom_tile = 0;
    int has_qcom_rp_transform = 0;
    int has_qcom_rotated_copy = 0;

    if (s_real_vkEnumerateDeviceExtensionProperties) {
        s_real_vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &ext_count, NULL);
        if (ext_count > 0) {
            VkExtensionProperties_compat_t* ext_list = (VkExtensionProperties_compat_t*)malloc(ext_count * sizeof(VkExtensionProperties_compat_t));
            if (ext_list) {
                if (s_real_vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &ext_count, ext_list) == 0) {
                    for (uint32_t i = 0; i < ext_count; ++i) {
                        if (strcmp(ext_list[i].extensionName, "VK_EXT_robustness2") == 0) has_robustness2 = 1;
                        if (strcmp(ext_list[i].extensionName, "VK_QCOM_tile_properties") == 0) has_qcom_tile = 1;
                        if (strcmp(ext_list[i].extensionName, "VK_QCOM_render_pass_transform") == 0) has_qcom_rp_transform = 1;
                        if (strcmp(ext_list[i].extensionName, "VK_QCOM_rotated_copy_commands") == 0) has_qcom_rotated_copy = 1;
                    }
                }
                free(ext_list);
            }
        }
    }

    // Check robustBufferAccess2 support
    uint32_t supp_rob2_buf = 0;
    uint32_t supp_rob2_img = 0;
    uint32_t supp_rob2_null = 0;
    if (has_robustness2 && s_real_vkGetPhysicalDeviceFeatures2) {
        VkPhysicalDeviceRobustness2FeaturesEXT_compat_t q_rob2;
        memset(&q_rob2, 0, sizeof(q_rob2));
        q_rob2.sType = 1000286000;
        VkPhysicalDeviceFeatures2_compat_t q_feat2;
        memset(&q_feat2, 0, sizeof(q_feat2));
        q_feat2.sType = 1000059000;
        q_feat2.pNext = &q_rob2;
        s_real_vkGetPhysicalDeviceFeatures2(physicalDevice, &q_feat2);
        supp_rob2_buf = q_rob2.robustBufferAccess2;
        supp_rob2_img = q_rob2.robustImageAccess2;
        supp_rob2_null = q_rob2.nullDescriptor;
    }

    // If robustBufferAccess2 is supported, force enable it in pNext chain
    static VkPhysicalDeviceRobustness2FeaturesEXT_compat_t s_rob2_chain;
    int found_rob2_in_pnext = 0;
    const void* cur_next = pCreateInfo->pNext;
    while (cur_next) {
        const uint32_t* header = (const uint32_t*)cur_next;
        if (header[0] == 1000286000 /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT */) {
            VkPhysicalDeviceRobustness2FeaturesEXT_compat_t* rob2 =
                (VkPhysicalDeviceRobustness2FeaturesEXT_compat_t*)cur_next;
            if (supp_rob2_buf) {
                rob2->robustBufferAccess2 = 1;
            }
            found_rob2_in_pnext = 1;
            __android_log_print(ANDROID_LOG_INFO, "NFSU2",
                "Adreno/Vulkan: VK_EXT_robustness2 active in pNext (robustBufferAccess2=%u, robustImageAccess2=%u, nullDescriptor=%u)",
                rob2->robustBufferAccess2, rob2->robustImageAccess2, rob2->nullDescriptor);
            break;
        }
        cur_next = *((const void* const*)((const char*)cur_next + 8));
    }

    if (!found_rob2_in_pnext && has_robustness2 && supp_rob2_buf) {
        memset(&s_rob2_chain, 0, sizeof(s_rob2_chain));
        s_rob2_chain.sType = 1000286000;
        s_rob2_chain.pNext = (void*)safeInfo.pNext;
        s_rob2_chain.robustBufferAccess2 = 1;
        s_rob2_chain.robustImageAccess2 = supp_rob2_img;
        s_rob2_chain.nullDescriptor = supp_rob2_null;
        safeInfo.pNext = &s_rob2_chain;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2",
            "Adreno/Vulkan: chained VK_EXT_robustness2 into vkCreateDevice (robustBufferAccess2=1, nullDescriptor=%u)",
            supp_rob2_null);
    }

    // Build extension list adding VK_EXT_robustness2 and Qualcomm Adreno extensions if supported
    const char** new_exts = NULL;
    uint32_t new_ext_count = safeInfo.enabledExtensionCount;
    uint32_t max_exts = new_ext_count + 8;
    new_exts = (const char**)malloc(max_exts * sizeof(const char*));
    if (new_exts) {
        for (uint32_t i = 0; i < safeInfo.enabledExtensionCount; ++i) {
            new_exts[i] = safeInfo.ppEnabledExtensionNames[i];
        }

        #define ADD_EXT_IF_MISSING(name) do { \
            int present = 0; \
            for (uint32_t k = 0; k < new_ext_count; ++k) { \
                if (strcmp(new_exts[k], name) == 0) { present = 1; break; } \
            } \
            if (!present) { \
                new_exts[new_ext_count++] = name; \
                __android_log_print(ANDROID_LOG_INFO, "NFSU2", "Adreno/Vulkan: Enabled device extension: %s", name); \
            } \
        } while (0)

        if (has_robustness2 && supp_rob2_buf) {
            ADD_EXT_IF_MISSING("VK_EXT_robustness2");
        }
        if (is_adreno) {
            if (has_qcom_tile) ADD_EXT_IF_MISSING("VK_QCOM_tile_properties");
            if (has_qcom_rp_transform) ADD_EXT_IF_MISSING("VK_QCOM_render_pass_transform");
            if (has_qcom_rotated_copy) ADD_EXT_IF_MISSING("VK_QCOM_rotated_copy_commands");
        }
        #undef ADD_EXT_IF_MISSING

        safeInfo.enabledExtensionCount = new_ext_count;
        safeInfo.ppEnabledExtensionNames = new_exts;
    }

    VkResult_t create_res = s_real_vkCreateDevice(physicalDevice, &safeInfo, pAllocator, pDevice);
    if (new_exts) free(new_exts);
    return create_res;
}

static __thread sigjmp_buf s_powervr_pipeline_jmp_buf;
static __thread volatile sig_atomic_t s_powervr_in_pipeline_create = 0;
static struct sigaction s_old_sa_segv;
static struct sigaction s_old_sa_bus;
static pthread_mutex_t s_powervr_sig_mu = PTHREAD_MUTEX_INITIALIZER;
static int s_powervr_handler_installed = 0;

static void powervr_sig_handler(int sig, siginfo_t *info, void *ucontext) {
    if (s_powervr_in_pipeline_create) {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2",
            "PowerVR Fault Guard: trapped signal %d (fault addr: %p) during vkCreateGraphicsPipelines, recovering...",
            sig, info ? info->si_addr : NULL);
        siglongjmp(s_powervr_pipeline_jmp_buf, sig);
    }
    struct sigaction *old_sa = (sig == SIGBUS) ? &s_old_sa_bus : &s_old_sa_segv;
    if (old_sa->sa_flags & SA_SIGINFO) {
        if (old_sa->sa_sigaction) old_sa->sa_sigaction(sig, info, ucontext);
    } else if (old_sa->sa_handler && old_sa->sa_handler != SIG_DFL && old_sa->sa_handler != SIG_IGN) {
        old_sa->sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

static void ensure_powervr_signal_handler(void) {
    pthread_mutex_lock(&s_powervr_sig_mu);
    if (!s_powervr_handler_installed) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = powervr_sig_handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &s_old_sa_segv);
        sigaction(SIGBUS, &sa, &s_old_sa_bus);
        s_powervr_handler_installed = 1;
    }
    pthread_mutex_unlock(&s_powervr_sig_mu);
}

static VkResult_t guarded_vkCreateGraphicsPipelines(
    VkDevice_t device,
    VkPipelineCache_t pipelineCache,
    uint32_t createInfoCount,
    const void *pCreateInfos,
    const void *pAllocator,
    VkPipeline_t *pPipelines) {

    if (!s_real_vkCreateGraphicsPipelines) {
        void *libvulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (libvulkan) {
            s_real_vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines_t)dlsym(libvulkan, "vkCreateGraphicsPipelines");
        }
    }
    if (!s_real_vkCreateGraphicsPipelines) {
        return -3; /* VK_ERROR_INITIALIZATION_FAILED */
    }

    ensure_powervr_signal_handler();

    s_powervr_in_pipeline_create = 1;
    int sig = sigsetjmp(s_powervr_pipeline_jmp_buf, 1);
    VkResult_t res;

    if (sig == 0) {
        res = s_real_vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2",
            "PowerVR Fault Guard: trapped crash (sig %d). Retrying with VK_NULL_HANDLE pipeline cache...", sig);
        s_powervr_in_pipeline_create = 1;
        int sig2 = sigsetjmp(s_powervr_pipeline_jmp_buf, 1);
        if (sig2 == 0) {
            res = s_real_vkCreateGraphicsPipelines(device, 0, createInfoCount, pCreateInfos, pAllocator, pPipelines);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, "NFSU2",
                "PowerVR Fault Guard: pipeline creation aborted safely (sig %d). Returning VK_ERROR_INITIALIZATION_FAILED", sig2);
            s_powervr_in_pipeline_create = 0;
            if (pPipelines && createInfoCount > 0) {
                for (uint32_t i = 0; i < createInfoCount; ++i) pPipelines[i] = 0;
            }
            return -3; /* VK_ERROR_INITIALIZATION_FAILED */
        }
    }
    s_powervr_in_pipeline_create = 0;
    return res;
}

static PFN_vkVoidFunction_t powervr_hook_vkGetDeviceProcAddr(VkDevice_t device, const char *pName) {
    if (!pName) return NULL;
    if (strcmp(pName, "vkCreateGraphicsPipelines") == 0) {
        if (s_real_vkGetDeviceProcAddr) {
            PFN_vkCreateGraphicsPipelines_t real_fn = (PFN_vkCreateGraphicsPipelines_t)s_real_vkGetDeviceProcAddr(device, pName);
            if (real_fn) s_real_vkCreateGraphicsPipelines = real_fn;
        }
        return (PFN_vkVoidFunction_t)guarded_vkCreateGraphicsPipelines;
    }
    if (s_real_vkGetDeviceProcAddr) {
        return s_real_vkGetDeviceProcAddr(device, pName);
    }
    return NULL;
}

static PFN_vkVoidFunction_t powervr_hook_vkGetInstanceProcAddr(VkInstance_t instance, const char *pName) {
    if (!pName) return NULL;
    if (strcmp(pName, "vkCreateDevice") == 0) {
        if (s_real_vkGetInstanceProcAddr) {
            PFN_vkCreateDevice_t real_cd = (PFN_vkCreateDevice_t)s_real_vkGetInstanceProcAddr(instance, pName);
            if (real_cd) s_real_vkCreateDevice = real_cd;
        }
        if (s_real_vkGetInstanceProcAddr && !s_real_vkGetPhysicalDeviceFeatures) {
            s_real_vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures_t)s_real_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures");
        }
        if (s_real_vkGetInstanceProcAddr && !s_real_vkGetPhysicalDeviceProperties) {
            s_real_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties_t)s_real_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
        }
        if (s_real_vkGetInstanceProcAddr && !s_real_vkGetPhysicalDeviceFeatures2) {
            s_real_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2_t)s_real_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2");
            if (!s_real_vkGetPhysicalDeviceFeatures2)
                s_real_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2_t)s_real_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2KHR");
        }
        if (s_real_vkGetInstanceProcAddr && !s_real_vkEnumerateDeviceExtensionProperties) {
            s_real_vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties_t)s_real_vkGetInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties");
        }
        return (PFN_vkVoidFunction_t)guarded_vkCreateDevice;
    }
    if (strcmp(pName, "vkCreateGraphicsPipelines") == 0) {
        if (s_real_vkGetInstanceProcAddr) {
            PFN_vkCreateGraphicsPipelines_t real_fn = (PFN_vkCreateGraphicsPipelines_t)s_real_vkGetInstanceProcAddr(instance, pName);
            if (real_fn) s_real_vkCreateGraphicsPipelines = real_fn;
        }
        return (PFN_vkVoidFunction_t)guarded_vkCreateGraphicsPipelines;
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        if (s_real_vkGetInstanceProcAddr) {
            PFN_vkGetDeviceProcAddr_t real_gdpa = (PFN_vkGetDeviceProcAddr_t)s_real_vkGetInstanceProcAddr(instance, pName);
            if (real_gdpa) s_real_vkGetDeviceProcAddr = real_gdpa;
        }
        return (PFN_vkVoidFunction_t)powervr_hook_vkGetDeviceProcAddr;
    }
    if (s_real_vkGetInstanceProcAddr) {
        return s_real_vkGetInstanceProcAddr(instance, pName);
    }
    return NULL;
}

void install_powervr_fault_guard(void) {
    typedef void (*fn_set_ipa_t)(PFN_vkGetInstanceProcAddr_t);
    typedef PFN_vkGetInstanceProcAddr_t (*fn_get_ipa_t)(void);

    fn_set_ipa_t set_ipa = (fn_set_ipa_t)dlsym(RTLD_DEFAULT, "dxvkAndroidSetInstanceProcAddr");
    fn_get_ipa_t get_ipa = (fn_get_ipa_t)dlsym(RTLD_DEFAULT, "dxvkAndroidGetInstanceProcAddr");
    if (!set_ipa || !get_ipa) return;

    PFN_vkGetInstanceProcAddr_t current = get_ipa();
    if (current == powervr_hook_vkGetInstanceProcAddr) return;

    s_real_vkGetInstanceProcAddr = current;
    void *libvulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (libvulkan) {
        PFN_vkCreateDevice_t sys_cd = (PFN_vkCreateDevice_t)dlsym(libvulkan, "vkCreateDevice");
        if (sys_cd) s_real_vkCreateDevice = sys_cd;
        PFN_vkGetPhysicalDeviceFeatures_t sys_gpf = (PFN_vkGetPhysicalDeviceFeatures_t)dlsym(libvulkan, "vkGetPhysicalDeviceFeatures");
        if (sys_gpf) s_real_vkGetPhysicalDeviceFeatures = sys_gpf;
        PFN_vkGetPhysicalDeviceProperties_t sys_gpp = (PFN_vkGetPhysicalDeviceProperties_t)dlsym(libvulkan, "vkGetPhysicalDeviceProperties");
        if (sys_gpp) s_real_vkGetPhysicalDeviceProperties = sys_gpp;
        PFN_vkGetPhysicalDeviceFeatures2_t sys_gpf2 = (PFN_vkGetPhysicalDeviceFeatures2_t)dlsym(libvulkan, "vkGetPhysicalDeviceFeatures2");
        if (!sys_gpf2) sys_gpf2 = (PFN_vkGetPhysicalDeviceFeatures2_t)dlsym(libvulkan, "vkGetPhysicalDeviceFeatures2KHR");
        if (sys_gpf2) s_real_vkGetPhysicalDeviceFeatures2 = sys_gpf2;
        PFN_vkEnumerateDeviceExtensionProperties_t sys_edep = (PFN_vkEnumerateDeviceExtensionProperties_t)dlsym(libvulkan, "vkEnumerateDeviceExtensionProperties");
        if (sys_edep) s_real_vkEnumerateDeviceExtensionProperties = sys_edep;
        PFN_vkCreateGraphicsPipelines_t sys_cgp = (PFN_vkCreateGraphicsPipelines_t)dlsym(libvulkan, "vkCreateGraphicsPipelines");
        if (sys_cgp) s_real_vkCreateGraphicsPipelines = sys_cgp;
        PFN_vkGetDeviceProcAddr_t sys_gdpa = (PFN_vkGetDeviceProcAddr_t)dlsym(libvulkan, "vkGetDeviceProcAddr");
        if (sys_gdpa) s_real_vkGetDeviceProcAddr = sys_gdpa;
    }

    set_ipa(powervr_hook_vkGetInstanceProcAddr);
    __android_log_print(ANDROID_LOG_INFO, "NFSU2", "PowerVR Fault Guard installed successfully for DXVK");
}
