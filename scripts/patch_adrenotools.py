#!/usr/bin/env python3
"""
patch_adrenotools.py
Patches libadrenotools to support loading ARM Mali PanVK (Mesa Panfrost) and modern Vulkan 1.3
drivers on Android devices without namespace resolution failures or silent fallbacks.
"""
import os
import sys

def patch_file(path, old, new):
    if not os.path.exists(path):
        print(f"[patch_adrenotools] Skipping {path} (not found)")
        return False
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    if new in content:
        print(f"[patch_adrenotools] Already patched: {path}")
        return True
    if old not in content:
        print(f"[patch_adrenotools] Target block not found in {path}")
        return False
    content = content.replace(old, new, 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[patch_adrenotools] Successfully patched {path}")
    return True

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    hook_impl_path = os.path.join(root, "third_party", "libadrenotools", "src", "hook", "hook_impl.cpp")
    
    old_target = """    if (extinfo->library_namespace == nullptr || !(extinfo->flags & ANDROID_DLEXT_USE_NAMESPACE)) {
        LOGI("hook_android_dlopen_ext: hook failed: namespace not supplied!");
        return fallback();
    }

    // customDriverDir will be empty if ADRENOTOOLS_DRIVER_CUSTOM isn't set therefore it's fine to have either way
    auto driverNs{android_create_namespace(filename, hook_params->customDriverDir.c_str(),
                                           hook_params->hookLibDir.c_str(), ANDROID_NAMESPACE_TYPE_SHARED,
                                           nullptr, extinfo->library_namespace)};
    if (!driverNs) {
        LOGI("hook_android_dlopen_ext: hook failed: namespace not supplied!");
        return fallback();
    }

    // We depend on libandroid which is unlikely to be in the supplied driver namespace so we have to link it over
    android_link_namespaces(driverNs, nullptr, "libandroid.so");"""

    new_replacement = """    android_namespace_t *parentNs = (extinfo && (extinfo->flags & ANDROID_DLEXT_USE_NAMESPACE)) ? extinfo->library_namespace : nullptr;
    if (!parentNs && android_get_exported_namespace) {
        for (const char *name : {"sphal", "vendor", "default", "system"}) {
            parentNs = android_get_exported_namespace(name);
            if (parentNs) break;
        }
    }

    // customDriverDir will be empty if ADRENOTOOLS_DRIVER_CUSTOM isn't set therefore it's fine to have either way
    auto driverNs{android_create_namespace(filename, hook_params->customDriverDir.c_str(),
                                           hook_params->hookLibDir.c_str(), ANDROID_NAMESPACE_TYPE_SHARED,
                                           nullptr, parentNs)};
    if (!driverNs) {
        LOGI("hook_android_dlopen_ext: hook failed: namespace creation failed!");
        return fallback();
    }

    // Link default namespace for all standard libs (libc, libm, liblog, libandroid, libnativewindow, libsync, libdl, etc.)
    linkernsbypass_link_namespace_to_default_all_libs(driverNs);

    // Link all exported namespaces for vendor HALs, mapper, gralloc, hidl, utils (needed by PanVK and modern Mesa)
    if (android_get_exported_namespace && android_link_namespaces_all_libs) {
        for (const char *name : {"sphal", "vendor", "system", "vndk", "default"}) {
            if (auto expNs{android_get_exported_namespace(name)}) {
                android_link_namespaces_all_libs(driverNs, expNs);
            }
        }
    }
    if (android_link_namespaces_all_libs && parentNs) {
        android_link_namespaces_all_libs(driverNs, parentNs);
    }"""

    patch_file(hook_impl_path, old_target, new_replacement)

    old_load = """    if (hook_params->featureFlags & ADRENOTOOLS_DRIVER_CUSTOM) {
        LOGI("hook_android_dlopen_ext: loading custom driver: %s%s", hook_params->customDriverDir.c_str(), hook_params->customDriverName.c_str());
        void *handle{android_dlopen_ext(hook_params->customDriverName.c_str(), flags, &newExtinfo)};
        if (!handle) {
            LOGI("hook_android_dlopen_ext: hook failed: failed to load custom driver: %s!", dlerror());
            return fallback();
        }

        return handle;"""

    new_load = """    if (hook_params->featureFlags & ADRENOTOOLS_DRIVER_CUSTOM) {
        LOGI("hook_android_dlopen_ext: loading custom driver: %s%s", hook_params->customDriverDir.c_str(), hook_params->customDriverName.c_str());
        void *handle{android_dlopen_ext(hook_params->customDriverName.c_str(), flags, &newExtinfo)};
        if (!handle) {
            std::string fullPath = hook_params->customDriverDir + hook_params->customDriverName;
            handle = android_dlopen_ext(fullPath.c_str(), flags, &newExtinfo);
        }
        if (!handle) {
            __android_log_print(ANDROID_LOG_ERROR, "NFSU2", "hook failed: failed to load custom driver %s%s: %s",
                hook_params->customDriverDir.c_str(), hook_params->customDriverName.c_str(), dlerror());
            return fallback();
        }
        __android_log_print(ANDROID_LOG_INFO, "NFSU2", "Successfully loaded custom Vulkan driver %s%s",
            hook_params->customDriverDir.c_str(), hook_params->customDriverName.c_str());
        return handle;"""

    patch_file(hook_impl_path, old_load, new_load)

if __name__ == "__main__":
    main()
