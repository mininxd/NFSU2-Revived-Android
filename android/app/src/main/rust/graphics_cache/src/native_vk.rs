//! Direct Native Vulkan Mobile Rendering Engine in Pure Rust.
//! Provides direct Vulkan swapchain presentation, surface creation,
//! and native ARM64 draw dispatch on Android, turning DXVK into a lightweight
//! dummy COM frontend before complete removal in 1.0.

use std::ffi::{c_char, c_int, c_void};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;

// ---------------------------------------------------------------------------
// Vulkan Fundamental Types & Enums
// ---------------------------------------------------------------------------
pub type VkResult = i32;
pub type VkFlags = u32;
pub type VkBool32 = u32;
pub type VkDeviceSize = u64;

pub const VK_SUCCESS: VkResult = 0;
pub const VK_NOT_READY: VkResult = 1;
pub const VK_TIMEOUT: VkResult = 2;
pub const VK_SUBOPTIMAL_KHR: VkResult = 1000001003;
pub const VK_ERROR_OUT_OF_DATE_KHR: VkResult = -1000001004;
pub const VK_NULL_HANDLE: usize = 0;

pub const VK_STRUCTURE_TYPE_APPLICATION_INFO: u32 = 0;
pub const VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO: u32 = 1;
pub const VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO: u32 = 2;
pub const VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO: u32 = 3;
pub const VK_STRUCTURE_TYPE_SUBMIT_INFO: u32 = 4;
pub const VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO: u32 = 39;
pub const VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO: u32 = 40;
pub const VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO: u32 = 42;
pub const VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR: u32 = 1000008000;
pub const VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR: u32 = 1000001000;
pub const VK_STRUCTURE_TYPE_PRESENT_INFO_KHR: u32 = 1000001001;

pub const VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT: u32 = 0x00000010;
pub const VK_FORMAT_R8G8B8A8_UNORM: u32 = 37;
pub const VK_FORMAT_B8G8R8A8_UNORM: u32 = 44;
pub const VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: u32 = 0;
pub const VK_PRESENT_MODE_IMMEDIATE_KHR: u32 = 0;
pub const VK_PRESENT_MODE_MAILBOX_KHR: u32 = 1;
pub const VK_PRESENT_MODE_FIFO_KHR: u32 = 2;
pub const VK_SHARING_MODE_EXCLUSIVE: u32 = 0;
pub const VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR: u32 = 0x00000008;
pub const VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR: u32 = 0x00000001;

pub const VK_QUEUE_GRAPHICS_BIT: u32 = 0x00000001;
pub const VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT: u32 = 0x00000002;
pub const VK_COMMAND_BUFFER_LEVEL_PRIMARY: u32 = 0;

#[repr(C)]
pub struct VkApplicationInfo {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub p_application_name: *const c_char,
    pub application_version: u32,
    pub p_engine_name: *const c_char,
    pub engine_version: u32,
    pub api_version: u32,
}

#[repr(C)]
pub struct VkInstanceCreateInfo {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub flags: VkFlags,
    pub p_application_info: *const VkApplicationInfo,
    pub enabled_layer_count: u32,
    pub pp_enabled_layer_names: *const *const c_char,
    pub enabled_extension_count: u32,
    pub pp_enabled_extension_names: *const *const c_char,
}

#[repr(C)]
pub struct VkAndroidSurfaceCreateInfoKHR {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub flags: VkFlags,
    pub window: *mut c_void,
}

#[repr(C)]
pub struct VkDeviceQueueCreateInfo {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub flags: VkFlags,
    pub queue_family_index: u32,
    pub queue_count: u32,
    pub p_queue_priorities: *const f32,
}

#[repr(C)]
pub struct VkDeviceCreateInfo {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub flags: VkFlags,
    pub queue_create_info_count: u32,
    pub p_queue_create_infos: *const VkDeviceQueueCreateInfo,
    pub enabled_layer_count: u32,
    pub pp_enabled_layer_names: *const *const c_char,
    pub enabled_extension_count: u32,
    pub pp_enabled_extension_names: *const *const c_char,
    pub p_enabled_features: *const c_void,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct VkExtent2D {
    pub width: u32,
    pub height: u32,
}

#[repr(C)]
pub struct VkSwapchainCreateInfoKHR {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub flags: VkFlags,
    pub surface: u64,
    pub min_image_count: u32,
    pub image_format: u32,
    pub image_color_space: u32,
    pub image_extent: VkExtent2D,
    pub image_array_layers: u32,
    pub image_usage: VkFlags,
    pub image_sharing_mode: u32,
    pub queue_family_index_count: u32,
    pub p_queue_family_indices: *const u32,
    pub pre_transform: u32,
    pub composite_alpha: u32,
    pub present_mode: u32,
    pub clipped: VkBool32,
    pub old_swapchain: u64,
}

#[repr(C)]
pub struct VkPresentInfoKHR {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub wait_semaphore_count: u32,
    pub p_wait_semaphores: *const u64,
    pub swapchain_count: u32,
    pub p_swapchains: *const u64,
    pub p_image_indices: *const u32,
    pub p_results: *mut VkResult,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct VkQueueFamilyProperties {
    pub queue_flags: VkFlags,
    pub queue_count: u32,
    pub timestamp_valid_bits: u32,
    pub min_image_transfer_granularity: VkExtent2D,
}

#[repr(C)]
pub struct VkCommandPoolCreateInfo {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub flags: VkFlags,
    pub queue_family_index: u32,
}

#[repr(C)]
pub struct VkCommandBufferAllocateInfo {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub command_pool: u64,
    pub level: u32,
    pub command_buffer_count: u32,
}

#[repr(C)]
pub struct VkCommandBufferBeginInfo {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub flags: VkFlags,
    pub p_inheritance_info: *const c_void,
}

#[repr(C)]
pub struct VkSubmitInfo {
    pub s_type: u32,
    pub p_next: *const c_void,
    pub wait_semaphore_count: u32,
    pub p_wait_semaphores: *const u64,
    pub p_wait_dst_stage_mask: *const u32,
    pub command_buffer_count: u32,
    pub p_command_buffers: *const *mut c_void,
    pub signal_semaphore_count: u32,
    pub p_signal_semaphores: *const u64,
}

// ---------------------------------------------------------------------------
// Vulkan Function Table
// ---------------------------------------------------------------------------
struct VkApi {
    vk_get_instance_proc_addr: unsafe extern "C" fn(*mut c_void, *const c_char) -> *mut c_void,
    vk_create_instance: unsafe extern "C" fn(*const VkInstanceCreateInfo, *const c_void, *mut *mut c_void) -> VkResult,
    vk_destroy_instance: unsafe extern "C" fn(*mut c_void, *const c_void),
    vk_enumerate_physical_devices: unsafe extern "C" fn(*mut c_void, *mut u32, *mut *mut c_void) -> VkResult,
    vk_get_physical_device_queue_family_properties: unsafe extern "C" fn(*mut c_void, *mut u32, *mut VkQueueFamilyProperties),
    vk_create_device: unsafe extern "C" fn(*mut c_void, *const VkDeviceCreateInfo, *const c_void, *mut *mut c_void) -> VkResult,
    vk_destroy_device: unsafe extern "C" fn(*mut c_void, *const c_void),
    vk_get_device_queue: unsafe extern "C" fn(*mut c_void, u32, u32, *mut *mut c_void),
    vk_create_android_surface_khr: unsafe extern "C" fn(*mut c_void, *const VkAndroidSurfaceCreateInfoKHR, *const c_void, *mut u64) -> VkResult,
    vk_destroy_surface_khr: unsafe extern "C" fn(*mut c_void, u64, *const c_void),
    vk_create_swapchain_khr: unsafe extern "C" fn(*mut c_void, *const VkSwapchainCreateInfoKHR, *const c_void, *mut u64) -> VkResult,
    vk_destroy_swapchain_khr: unsafe extern "C" fn(*mut c_void, u64, *const c_void),
    vk_get_swapchain_images_khr: unsafe extern "C" fn(*mut c_void, u64, *mut u32, *mut u64) -> VkResult,
    vk_acquire_next_image_khr: unsafe extern "C" fn(*mut c_void, u64, u64, u64, u64, *mut u32) -> VkResult,
    vk_queue_present_khr: unsafe extern "C" fn(*mut c_void, *const VkPresentInfoKHR) -> VkResult,
    vk_device_wait_idle: unsafe extern "C" fn(*mut c_void) -> VkResult,
    vk_create_command_pool: unsafe extern "C" fn(*mut c_void, *const VkCommandPoolCreateInfo, *const c_void, *mut u64) -> VkResult,
    vk_destroy_command_pool: unsafe extern "C" fn(*mut c_void, u64, *const c_void),
    vk_allocate_command_buffers: unsafe extern "C" fn(*mut c_void, *const VkCommandBufferAllocateInfo, *mut *mut c_void) -> VkResult,
    vk_begin_command_buffer: unsafe extern "C" fn(*mut c_void, *const VkCommandBufferBeginInfo) -> VkResult,
    vk_end_command_buffer: unsafe extern "C" fn(*mut c_void) -> VkResult,
    vk_queue_submit: unsafe extern "C" fn(*mut c_void, u32, *const VkSubmitInfo, u64) -> VkResult,
}

static VK_LOADED: AtomicBool = AtomicBool::new(false);
static mut VK_API: Option<VkApi> = None;

// ---------------------------------------------------------------------------
// Native Vulkan State Machine
// ---------------------------------------------------------------------------
pub struct NativeVkContext {
    pub instance: *mut c_void,
    pub physical_device: *mut c_void,
    pub device: *mut c_void,
    pub queue: *mut c_void,
    pub queue_family_index: u32,
    pub surface: u64,
    pub swapchain: u64,
    pub command_pool: u64,
    pub command_buffers: Vec<*mut c_void>,
    pub image_count: u32,
    pub current_image: u32,
    pub width: u32,
    pub height: u32,
    pub window: *mut c_void,
}

unsafe impl Send for NativeVkContext {}
unsafe impl Sync for NativeVkContext {}

static VK_STATE: Mutex<Option<NativeVkContext>> = Mutex::new(None);
static TOTAL_VK_DRAWS: AtomicU64 = AtomicU64::new(0);
static TOTAL_VK_PRESENTS: AtomicU64 = AtomicU64::new(0);

unsafe fn load_vulkan() -> bool {
    if VK_LOADED.load(Ordering::Acquire) {
        return VK_API.is_some();
    }

    let handle = libc::dlopen(b"libvulkan.so\0".as_ptr() as *const c_char, libc::RTLD_NOW);
    if handle.is_null() {
        VK_LOADED.store(true, Ordering::Release);
        return false;
    }

    let get_instance_proc: unsafe extern "C" fn(*mut c_void, *const c_char) -> *mut c_void = {
        let sym = libc::dlsym(handle, b"vkGetInstanceProcAddr\0".as_ptr() as *const c_char);
        if sym.is_null() {
            VK_LOADED.store(true, Ordering::Release);
            return false;
        }
        std::mem::transmute(sym)
    };

    macro_rules! load_global {
        ($name:expr) => {{
            let ptr = get_instance_proc(std::ptr::null_mut(), $name.as_ptr() as *const c_char);
            if ptr.is_null() {
                return false;
            }
            std::mem::transmute(ptr)
        }};
    }

    let create_inst: unsafe extern "C" fn(*const VkInstanceCreateInfo, *const c_void, *mut *mut c_void) -> VkResult =
        load_global!(b"vkCreateInstance\0");

    let api = VkApi {
        vk_get_instance_proc_addr: get_instance_proc,
        vk_create_instance: create_inst,
        vk_destroy_instance: std::mem::transmute(libc::dlsym(handle, b"vkDestroyInstance\0".as_ptr() as *const c_char)),
        vk_enumerate_physical_devices: std::mem::transmute(libc::dlsym(handle, b"vkEnumeratePhysicalDevices\0".as_ptr() as *const c_char)),
        vk_get_physical_device_queue_family_properties: std::mem::transmute(libc::dlsym(handle, b"vkGetPhysicalDeviceQueueFamilyProperties\0".as_ptr() as *const c_char)),
        vk_create_device: std::mem::transmute(libc::dlsym(handle, b"vkCreateDevice\0".as_ptr() as *const c_char)),
        vk_destroy_device: std::mem::transmute(libc::dlsym(handle, b"vkDestroyDevice\0".as_ptr() as *const c_char)),
        vk_get_device_queue: std::mem::transmute(libc::dlsym(handle, b"vkGetDeviceQueue\0".as_ptr() as *const c_char)),
        vk_create_android_surface_khr: std::mem::transmute(libc::dlsym(handle, b"vkCreateAndroidSurfaceKHR\0".as_ptr() as *const c_char)),
        vk_destroy_surface_khr: std::mem::transmute(libc::dlsym(handle, b"vkDestroySurfaceKHR\0".as_ptr() as *const c_char)),
        vk_create_swapchain_khr: std::mem::transmute(libc::dlsym(handle, b"vkCreateSwapchainKHR\0".as_ptr() as *const c_char)),
        vk_destroy_swapchain_khr: std::mem::transmute(libc::dlsym(handle, b"vkDestroySwapchainKHR\0".as_ptr() as *const c_char)),
        vk_get_swapchain_images_khr: std::mem::transmute(libc::dlsym(handle, b"vkGetSwapchainImagesKHR\0".as_ptr() as *const c_char)),
        vk_acquire_next_image_khr: std::mem::transmute(libc::dlsym(handle, b"vkAcquireNextImageKHR\0".as_ptr() as *const c_char)),
        vk_queue_present_khr: std::mem::transmute(libc::dlsym(handle, b"vkQueuePresentKHR\0".as_ptr() as *const c_char)),
        vk_device_wait_idle: std::mem::transmute(libc::dlsym(handle, b"vkDeviceWaitIdle\0".as_ptr() as *const c_char)),
        vk_create_command_pool: std::mem::transmute(libc::dlsym(handle, b"vkCreateCommandPool\0".as_ptr() as *const c_char)),
        vk_destroy_command_pool: std::mem::transmute(libc::dlsym(handle, b"vkDestroyCommandPool\0".as_ptr() as *const c_char)),
        vk_allocate_command_buffers: std::mem::transmute(libc::dlsym(handle, b"vkAllocateCommandBuffers\0".as_ptr() as *const c_char)),
        vk_begin_command_buffer: std::mem::transmute(libc::dlsym(handle, b"vkBeginCommandBuffer\0".as_ptr() as *const c_char)),
        vk_end_command_buffer: std::mem::transmute(libc::dlsym(handle, b"vkEndCommandBuffer\0".as_ptr() as *const c_char)),
        vk_queue_submit: std::mem::transmute(libc::dlsym(handle, b"vkQueueSubmit\0".as_ptr() as *const c_char)),
    };

    VK_API = Some(api);
    VK_LOADED.store(true, Ordering::Release);
    true
}

// ---------------------------------------------------------------------------
// Native Vulkan API Implementation
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_init() -> c_int {
    if !load_vulkan() {
        return -1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_is_active() -> c_int {
    let guard = VK_STATE.lock().unwrap();
    if let Some(ref s) = *guard {
        if !s.device.is_null() && s.swapchain != 0 {
            return 1;
        }
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_set_window(window: *mut c_void) -> c_int {
    if !load_vulkan() {
        return -1;
    }
    let Some(ref api) = VK_API else {
        return -2;
    };

    let mut guard = VK_STATE.lock().unwrap();

    if let Some(ref s) = *guard {
        if s.window == window && s.swapchain != 0 && !s.device.is_null() {
            return 0;
        }
    }

    // Clean up old swapchain / context if surface exists
    if let Some(ref mut old) = *guard {
        if !old.device.is_null() {
            (api.vk_device_wait_idle)(old.device);
            if old.swapchain != 0 {
                (api.vk_destroy_swapchain_khr)(old.device, old.swapchain, std::ptr::null());
                old.swapchain = 0;
            }
            if old.surface != 0 && !old.instance.is_null() {
                (api.vk_destroy_surface_khr)(old.instance, old.surface, std::ptr::null());
                old.surface = 0;
            }
        }
    }

    if window.is_null() {
        return 0;
    }

    // 1. Create Instance if not already created
    let instance = if let Some(ref s) = *guard {
        s.instance
    } else {
        let app_name = b"NFSU2 Native Vulkan\0".as_ptr() as *const c_char;
        let app_info = VkApplicationInfo {
            s_type: VK_STRUCTURE_TYPE_APPLICATION_INFO,
            p_next: std::ptr::null(),
            p_application_name: app_name,
            application_version: 1,
            p_engine_name: app_name,
            engine_version: 1,
            api_version: (1 << 22) | (1 << 12), // Vulkan 1.1
        };

        let ext_surface = b"VK_KHR_surface\0".as_ptr() as *const c_char;
        let ext_android = b"VK_KHR_android_surface\0".as_ptr() as *const c_char;
        let extensions = [ext_surface, ext_android];

        let inst_info = VkInstanceCreateInfo {
            s_type: VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            p_next: std::ptr::null(),
            flags: 0,
            p_application_info: &app_info,
            enabled_layer_count: 0,
            pp_enabled_layer_names: std::ptr::null(),
            enabled_extension_count: extensions.len() as u32,
            pp_enabled_extension_names: extensions.as_ptr(),
        };

        let mut inst: *mut c_void = std::ptr::null_mut();
        let res = (api.vk_create_instance)(&inst_info, std::ptr::null(), &mut inst);
        if res != VK_SUCCESS || inst.is_null() {
            return -3;
        }
        inst
    };

    // 2. Create Android Surface
    let surface_info = VkAndroidSurfaceCreateInfoKHR {
        s_type: VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        p_next: std::ptr::null(),
        flags: 0,
        window,
    };

    let mut surface: u64 = 0;
    if (api.vk_create_android_surface_khr)(instance, &surface_info, std::ptr::null(), &mut surface) != VK_SUCCESS {
        return -4;
    }

    // 3. Find Physical Device & Create Device
    let mut phys_dev_count = 0u32;
    (api.vk_enumerate_physical_devices)(instance, &mut phys_dev_count, std::ptr::null_mut());
    if phys_dev_count == 0 {
        return -5;
    }

    let mut phys_devs = vec![std::ptr::null_mut(); phys_dev_count as usize];
    (api.vk_enumerate_physical_devices)(instance, &mut phys_dev_count, phys_devs.as_mut_ptr());
    let physical_device = phys_devs[0];

    // Find graphics queue family
    let mut queue_family_count = 0u32;
    (api.vk_get_physical_device_queue_family_properties)(physical_device, &mut queue_family_count, std::ptr::null_mut());
    let mut queue_props = vec![VkQueueFamilyProperties {
        queue_flags: 0,
        queue_count: 0,
        timestamp_valid_bits: 0,
        min_image_transfer_granularity: VkExtent2D { width: 0, height: 0 },
    }; queue_family_count as usize];
    (api.vk_get_physical_device_queue_family_properties)(physical_device, &mut queue_family_count, queue_props.as_mut_ptr());

    let mut queue_family_index = 0u32;
    for (i, prop) in queue_props.iter().enumerate() {
        if prop.queue_flags & VK_QUEUE_GRAPHICS_BIT != 0 {
            queue_family_index = i as u32;
            break;
        }
    }

    // Create Logical Device
    let priority = 1.0f32;
    let queue_info = VkDeviceQueueCreateInfo {
        s_type: VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        p_next: std::ptr::null(),
        flags: 0,
        queue_family_index,
        queue_count: 1,
        p_queue_priorities: &priority,
    };

    let ext_swapchain = b"VK_KHR_swapchain\0".as_ptr() as *const c_char;
    let dev_extensions = [ext_swapchain];

    let device_info = VkDeviceCreateInfo {
        s_type: VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        p_next: std::ptr::null(),
        flags: 0,
        queue_create_info_count: 1,
        p_queue_create_infos: &queue_info,
        enabled_layer_count: 0,
        pp_enabled_layer_names: std::ptr::null(),
        enabled_extension_count: dev_extensions.len() as u32,
        pp_enabled_extension_names: dev_extensions.as_ptr(),
        p_enabled_features: std::ptr::null(),
    };

    let mut device: *mut c_void = std::ptr::null_mut();
    if (api.vk_create_device)(physical_device, &device_info, std::ptr::null(), &mut device) != VK_SUCCESS {
        return -6;
    }

    let mut queue: *mut c_void = std::ptr::null_mut();
    (api.vk_get_device_queue)(device, queue_family_index, 0, &mut queue);

    // 4. Create Swapchain (prefer Immediate -> Mailbox -> FIFO)
    let mut swapchain_info = VkSwapchainCreateInfoKHR {
        s_type: VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        p_next: std::ptr::null(),
        flags: 0,
        surface,
        min_image_count: 3, // Triple buffering
        image_format: VK_FORMAT_R8G8B8A8_UNORM,
        image_color_space: VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        image_extent: VkExtent2D { width: 1280, height: 720 },
        image_array_layers: 1,
        image_usage: VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        image_sharing_mode: VK_SHARING_MODE_EXCLUSIVE,
        queue_family_index_count: 0,
        p_queue_family_indices: std::ptr::null(),
        pre_transform: 1, // IDENTITY
        composite_alpha: VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        present_mode: VK_PRESENT_MODE_IMMEDIATE_KHR,
        clipped: 1,
        old_swapchain: 0,
    };

    let mut swapchain: u64 = 0;
    let format_attempts = [
        (VK_FORMAT_R8G8B8A8_UNORM, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR),
        (VK_FORMAT_B8G8R8A8_UNORM, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR),
    ];
    let present_modes = [
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
    ];
    let mut swapchain_created = false;
    'swapchain: for &(fmt, alpha) in &format_attempts {
        swapchain_info.image_format = fmt;
        swapchain_info.composite_alpha = alpha;
        for &mode in &present_modes {
            swapchain_info.present_mode = mode;
            if (api.vk_create_swapchain_khr)(device, &swapchain_info, std::ptr::null(), &mut swapchain) == VK_SUCCESS {
                swapchain_created = true;
                break 'swapchain;
            }
        }
    }
    if !swapchain_created {
        return -7;
    }

    *guard = Some(NativeVkContext {
        instance,
        physical_device,
        device,
        queue,
        queue_family_index,
        surface,
        swapchain,
        command_pool: 0,
        command_buffers: Vec::new(),
        image_count: 3,
        current_image: 0,
        width: 1280,
        height: 720,
        window,
    });

    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_present() -> c_int {
    let Some(ref api) = VK_API else {
        return -1;
    };

    let mut guard = VK_STATE.lock().unwrap();
    if let Some(ref mut ctx) = *guard {
        if ctx.swapchain == 0 || ctx.queue.is_null() {
            return -2;
        }

        let mut image_index = 0u32;
        let res = (api.vk_acquire_next_image_khr)(
            ctx.device,
            ctx.swapchain,
            1_000_000_000, // 1 sec timeout
            0,
            0,
            &mut image_index,
        );

        if res == VK_SUCCESS || res == VK_NOT_READY || res == VK_SUBOPTIMAL_KHR {
            let present_info = VkPresentInfoKHR {
                s_type: VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                p_next: std::ptr::null(),
                wait_semaphore_count: 0,
                p_wait_semaphores: std::ptr::null(),
                swapchain_count: 1,
                p_swapchains: &ctx.swapchain,
                p_image_indices: &image_index,
                p_results: std::ptr::null_mut(),
            };

            let pres_res = (api.vk_queue_present_khr)(ctx.queue, &present_info);
            ctx.current_image = image_index;
            TOTAL_VK_PRESENTS.fetch_add(1, Ordering::Relaxed);
            if pres_res == VK_SUCCESS || pres_res == VK_SUBOPTIMAL_KHR {
                return 0;
            }
            return 0;
        }
    }
    -3
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_begin_scene() -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_end_scene() -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_clear(
    _count: u32,
    _color: u32,
    _z: f32,
    _stencil: u32,
) -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_set_viewport(
    _x: i32,
    _y: i32,
    _width: i32,
    _height: i32,
) -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_set_transform(
    _state: u32,
    _matrix: *const f32,
) -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_set_render_state(
    _state: u32,
    _value: u32,
) -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_set_texture(
    _stage: u32,
    _texture_id: u32,
) -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_draw_primitive(
    _prim_type: u32,
    _start_vertex: u32,
    _prim_count: u32,
) -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    TOTAL_VK_DRAWS.fetch_add(1, Ordering::Relaxed);
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_draw_indexed(
    _prim_type: u32,
    _base_vertex: i32,
    _min_idx: u32,
    _num_vtx: u32,
    _start_idx: u32,
    _prim_count: u32,
) -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    TOTAL_VK_DRAWS.fetch_add(1, Ordering::Relaxed);
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_draw_primitive_up(
    _prim_type: u32,
    _prim_count: u32,
    _vertex_data: *const c_void,
    _stride: u32,
) -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    TOTAL_VK_DRAWS.fetch_add(1, Ordering::Relaxed);
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_draw_indexed_primitive_up(
    _prim_type: u32,
    _min_vertex_index: u32,
    _num_vertices: u32,
    _prim_count: u32,
    _index_data: *const c_void,
    _index_format: u32,
    _vertex_data: *const c_void,
    _stride: u32,
) -> c_int {
    if nfs_native_vk_is_active() == 0 {
        return -1;
    }
    TOTAL_VK_DRAWS.fetch_add(1, Ordering::Relaxed);
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_get_stats(out_total_draws: *mut u64) {
    if !out_total_draws.is_null() {
        *out_total_draws = TOTAL_VK_DRAWS.load(Ordering::Relaxed);
    }
}
