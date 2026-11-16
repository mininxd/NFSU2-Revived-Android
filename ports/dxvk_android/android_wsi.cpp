#include <cstdint>
#include "../../android/app/src/main/cpp/android_resolution.h"
#ifdef __ANDROID__
#include <android/native_window.h>
#else
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#undef None
#undef Always
#undef Status
struct ANativeWindow {Display* display;Window window;uint32_t width,height;};
#endif
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include "src/dxvk/dxvk_presenter.h"
#include "src/wsi/wsi_window.h"
#include "src/wsi/wsi_monitor.h"
#include "src/wsi/wsi_platform.h"
#include "src/dxvk/dxvk_platform_exts.h"
#include "src/vulkan/vulkan_util.h"

namespace {
std::mutex windowMutex;
ANativeWindow* currentWindow=nullptr;
#ifdef __ANDROID__
uintptr_t windowGeneration=0x1000;
#endif
HWND currentHandle=nullptr;
uint32_t screenWidth=1280,screenHeight=720;
uint32_t renderWidth=0,renderHeight=0;
// Buffer size set on the current window by resizeWindow (D3D9 Reset), 0 = the
// window's own size. The swap chain must use it: a screen-sized swap chain on
// smaller buffers is cropped to their top-left corner by Android.
uint32_t bufferWidth=0,bufferHeight=0;
HMONITOR monitor(){return reinterpret_cast<HMONITOR>(uintptr_t{1});}
// The Vulkan driver DXVK talks to: the linked system loader, or an imported
// adrenotools driver chosen in the launcher (set before the device exists).
std::atomic<PFN_vkGetInstanceProcAddr> instanceProcAddr{nullptr};
}
#define DXVK_EXPORT __attribute__((visibility("default")))

extern "C" DXVK_EXPORT void dxvkAndroidSetInstanceProcAddr(PFN_vkGetInstanceProcAddr entry){instanceProcAddr=entry;}
extern "C" DXVK_EXPORT PFN_vkGetInstanceProcAddr dxvkAndroidGetInstanceProcAddr(){
    return instanceProcAddr.load();
}
extern "C" DXVK_EXPORT void dxvkAndroidSetRenderSize(uint32_t width,uint32_t height){std::lock_guard<std::mutex> guard(windowMutex);renderWidth=width;renderHeight=height;
#ifdef __ANDROID__
    if(currentWindow&&width&&height&&
       ANativeWindow_setBuffersGeometry(currentWindow,width,height,0)==0){bufferWidth=width;bufferHeight=height;}
#endif
}
extern "C" DXVK_EXPORT HWND dxvkAndroidGetWindowHandle(){std::lock_guard<std::mutex> guard(windowMutex);return currentHandle;}
extern "C" DXVK_EXPORT void dxvkAndroidSetWindow(ANativeWindow* window){
    std::lock_guard<std::mutex> guard(windowMutex);
    if(window==currentWindow)return;
#ifdef __ANDROID__
    if(window){ANativeWindow_acquire(window);screenWidth=ANativeWindow_getWidth(window);screenHeight=ANativeWindow_getHeight(window);}
    if(currentWindow)ANativeWindow_release(currentWindow);
#endif
    currentWindow=window;bufferWidth=bufferHeight=0;
#ifdef __ANDROID__
    // Below native resolution, present at the render size and let the Android
    // compositor scale the surface: the swap chain blit no longer writes a
    // full-screen image every frame.
    if(window&&renderWidth&&renderHeight&&
       ANativeWindow_setBuffersGeometry(window,renderWidth,renderHeight,0)==0){bufferWidth=renderWidth;bufferHeight=renderHeight;}
    currentHandle=window?reinterpret_cast<HWND>(uintptr_t{0x1000}):nullptr;
#else
    currentHandle=window;
#endif
}
#ifndef __ANDROID__
extern "C" ANativeWindow* dxvkCreateTestWindow(){
    XInitThreads();auto display=XOpenDisplay(nullptr);if(!display)return nullptr;
    Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,1280,720,0,0,0);
    XMapWindow(display,window);XSync(display,False);return new ANativeWindow{display,window,1280,720};
}
extern "C" void dxvkDestroyTestWindow(ANativeWindow* window){if(!window)return;XDestroyWindow(window->display,window->window);XCloseDisplay(window->display);delete window;}
#endif
namespace dxvk::wsi {
void init(){}
void quit(){}
std::vector<const char *> getInstanceExtensions(){
    return {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __ANDROID__
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#else
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
    };
}
VkResult createSurface(
        HWND                      window,
        PFN_vkGetInstanceProcAddr pfnVkGetInstanceProcAddr,
        VkInstance                instance,
        VkSurfaceKHR*             surface){
    std::lock_guard<std::mutex> guard(windowMutex);
#ifdef __ANDROID__
    if(!currentWindow||!window)return VK_ERROR_SURFACE_LOST_KHR;
    auto create=reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        pfnVkGetInstanceProcAddr ? pfnVkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR") :
        dxvkAndroidGetInstanceProcAddr()(instance,"vkCreateAndroidSurfaceKHR"));
    if(!create)return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};info.window=currentWindow;
    return create(instance,&info,nullptr,surface);
#else
    if(!currentWindow||window!=currentHandle)return VK_ERROR_SURFACE_LOST_KHR;
    auto create=reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
        pfnVkGetInstanceProcAddr ? pfnVkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR") :
        vkGetInstanceProcAddr(instance,"vkCreateXlibSurfaceKHR"));
    if(!create)return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkXlibSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};info.dpy=currentWindow->display;info.window=currentWindow->window;
    return create(instance,&info,nullptr,surface);
#endif
}
HMONITOR getDefaultMonitor(){return monitor();}
HMONITOR enumMonitors(uint32_t index){return index?nullptr:monitor();}
HMONITOR enumMonitors(const LUID *adapterLUID[], uint32_t numLUIDs, uint32_t index){(void)adapterLUID;(void)numLUIDs;return enumMonitors(index);}
bool getDisplayName(HMONITOR handle,WCHAR (&name)[32]){if(handle!=monitor())return false;const wchar_t display[]=L"\\\\.\\DISPLAY1";std::fill(std::begin(name),std::end(name),0);std::copy(std::begin(display),std::end(display),name);return true;}
bool getDesktopCoordinates(HMONITOR handle,RECT* rect){if(handle!=monitor()||!rect)return false;std::lock_guard<std::mutex> guard(windowMutex);*rect={0,0,static_cast<LONG>(bufferWidth?bufferWidth:screenWidth),static_cast<LONG>(bufferHeight?bufferHeight:screenHeight)};return true;}
bool getCurrentDisplayMode(HMONITOR handle,WsiMode* mode){if(handle!=monitor()||!mode)return false;std::lock_guard<std::mutex> guard(windowMutex);*mode={screenWidth,screenHeight,{60,1},32,false};return true;}
bool getDesktopDisplayMode(HMONITOR handle,WsiMode* mode){return getCurrentDisplayMode(handle,mode);}
bool getDisplayMode(HMONITOR handle,uint32_t index,WsiMode* mode){
    if(handle!=monitor()||!mode)return false;
    std::lock_guard<std::mutex> guard(windowMutex);
    if(!renderWidth||!renderHeight){
        static constexpr AndroidRenderMode pcModes[]={{640,480},{800,600},{1024,768},{1280,960},{1280,1024},{1600,1200}};
        if(index>6)return false;
        auto selected=index==6?AndroidRenderMode{screenWidth,screenHeight}:pcModes[index];
        *mode={selected.width,selected.height,{60,1},32,false};return true;
    }
    if(index>=6)return false;
    uint32_t activeSlot = 0;
    auto sizes=androidRenderModes(screenWidth,screenHeight,renderWidth?renderWidth:screenWidth,renderHeight?renderHeight:screenHeight, activeSlot);
    *mode={sizes[index].width,sizes[index].height,{60,1},32,false};return true;
}
WsiEdidData getMonitorEdid(HMONITOR handle){(void)handle;return {};}
std::optional<WsiDisplayMetadata> parseColorimetryInfo(const WsiEdidData& edidData){(void)edidData;return std::nullopt;}
void getWindowSize(HWND window,uint32_t* width,uint32_t* height){
    std::lock_guard<std::mutex> guard(windowMutex);
#ifdef __ANDROID__
    bool valid=currentWindow&&window;
#else
    bool valid=currentWindow&&window==currentHandle;
#endif
    if(width)*width=valid?(bufferWidth?bufferWidth:screenWidth):0;
    if(height)*height=valid?(bufferHeight?bufferHeight:screenHeight):0;
}
void resizeWindow(HWND window,DxvkWindowState*,uint32_t width,uint32_t height){
    std::lock_guard<std::mutex> guard(windowMutex);
#ifdef __ANDROID__
    uint32_t targetW = renderWidth ? renderWidth : width;
    uint32_t targetH = renderHeight ? renderHeight : height;
    if(currentWindow&&window&&ANativeWindow_setBuffersGeometry(currentWindow,targetW,targetH,0)==0){
        bufferWidth=targetW;bufferHeight=targetH;
    }
#else
    (void)window;(void)width;(void)height;
#endif
}
void saveWindowState(HWND,DxvkWindowState*,bool){}
void restoreWindowState(HWND,DxvkWindowState*,bool){}
void updateFullscreenWindow(HMONITOR,HWND,bool){}
bool isWindow(HWND window){
    std::lock_guard<std::mutex> guard(windowMutex);
#ifdef __ANDROID__
    return currentWindow&&window;
#else
    return currentWindow&&window==currentHandle;
#endif
}
bool isMinimized(HWND window){(void)window;return false;}
bool isOccluded(HWND window){(void)window;return false;}
HMONITOR getWindowMonitor(HWND window){return isWindow(window)?monitor():nullptr;}
bool setWindowMode(HMONITOR handle,HWND window,DxvkWindowState*,const WsiMode&){return handle==monitor()&&isWindow(window);}
bool enterFullscreenMode(HMONITOR handle,HWND window,DxvkWindowState*,bool){return handle==monitor()&&isWindow(window);}
bool leaveFullscreenMode(HWND window,DxvkWindowState*){return isWindow(window);}
bool restoreDisplayMode(){return true;}
}
