// DirectInput 8 Force Feedback & Haptic Rumble Bridge backed by Rust.
// Connects guest x86 SPEED2.EXE DirectInput device and effect methods
// to the high-performance Rust implementation in libgraphics_cache.so.

#ifndef GUEST_DINPUT_FEEDBACK_INC
#define GUEST_DINPUT_FEEDBACK_INC

#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <map>

// DirectInput 8 Effect Object (Kind 5)
struct GuestDirectInputEffect {
    uint32_t effectId = 0;
    uint32_t table = 0;
    uint32_t references = 1;
};

static std::map<uint32_t, GuestDirectInputEffect> s_effectObjects;

// Resolves Rust exports dynamically
namespace DInputRust {
    inline uint32_t createEffect(uint32_t guid, uint32_t duration, uint32_t gain, int32_t mag, uint32_t period, uint32_t* outId) {
        static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t, uint32_t, int32_t, uint32_t, uint32_t*)>(
            dlsym(RTLD_DEFAULT, "rust_dinput_create_effect")
        );
        if (s_fn) return s_fn(guid, duration, gain, mag, period, outId);
        static uint32_t s_fallbackId = 100;
        if (outId) *outId = ++s_fallbackId;
        return 0; // DI_OK
    }

    inline uint32_t destroyEffect(uint32_t effectId) {
        static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t)>(
            dlsym(RTLD_DEFAULT, "rust_dinput_destroy_effect")
        );
        return s_fn ? s_fn(effectId) : 0;
    }

    inline uint32_t startEffect(uint32_t effectId, uint32_t iterations, uint32_t flags) {
        static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t, uint32_t)>(
            dlsym(RTLD_DEFAULT, "rust_dinput_start_effect")
        );
        return s_fn ? s_fn(effectId, iterations, flags) : 0;
    }

    inline uint32_t stopEffect(uint32_t effectId) {
        static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t)>(
            dlsym(RTLD_DEFAULT, "rust_dinput_stop_effect")
        );
        return s_fn ? s_fn(effectId) : 0;
    }

    inline uint32_t getEffectStatus(uint32_t effectId, uint32_t* outStatus) {
        static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t*)>(
            dlsym(RTLD_DEFAULT, "rust_dinput_get_effect_status")
        );
        if (s_fn) return s_fn(effectId, outStatus);
        if (outStatus) *outStatus = 1; // DIEGES_PLAYING
        return 0;
    }

    inline uint32_t setEffectParams(uint32_t effectId, uint32_t duration, uint32_t gain, int32_t mag, uint32_t period) {
        static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t, uint32_t, int32_t, uint32_t)>(
            dlsym(RTLD_DEFAULT, "rust_dinput_set_effect_params")
        );
        return s_fn ? s_fn(effectId, duration, gain, mag, period) : 0;
    }

    inline uint32_t sendCommand(uint32_t cmd) {
        static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t)>(
            dlsym(RTLD_DEFAULT, "rust_dinput_send_ff_command")
        );
        return s_fn ? s_fn(cmd) : 0;
    }

    inline uint32_t getState(uint32_t* outState) {
        static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t*)>(
            dlsym(RTLD_DEFAULT, "rust_dinput_get_ff_state")
        );
        if (s_fn) return s_fn(outState);
        if (outState) *outState = 0x51; // DIGFFS_POWERON | DIGFFS_ACTUATORSON | DIGFFS_EMPTY
        return 0;
    }

    inline uint32_t getMotorSpeeds(float* left, float* right) {
        static auto s_fn = reinterpret_cast<uint32_t(*)(float*, float*)>(
            dlsym(RTLD_DEFAULT, "rust_dinput_get_motor_speeds")
        );
        return s_fn ? s_fn(left, right) : 0;
    }
}

// Allocates a guest IDirectInputEffect COM object with 32 vtable slots pointing to kind 5
inline uint32_t createGuestInputEffect(uint32_t rustEffectId) {
    uint32_t object = allocate(4);
    uint32_t table = allocate(32 * 4);
    for (uint32_t slot = 0; slot < 32; ++slot) {
        write32(table + slot * 4, inputMethodBase + 5 * 0x1000 + slot * 16);
    }
    write32(object, table);
    s_effectObjects[object] = {rustEffectId, table, 1};
    return object;
}

// Dispatches IDirectInputEffect methods (kind 5)
inline void dinputEffectDispatch(uint32_t self, uint32_t method) {
    auto it = s_effectObjects.find(self);
    if (it == s_effectObjects.end()) {
        ret(0x80004005u, 1); // E_FAIL
        return;
    }
    auto& effect = it->second;

    switch (method) {
        case 0: { // QueryInterface(this, riid, ppvObj)
            uint32_t outObj = arg(2);
            if (!outObj) {
                ret(0x80004003u, 3); // E_POINTER
                return;
            }
            effect.references++;
            write32(outObj, self);
            ret(0, 3); // S_OK
            return;
        }
        case 1: { // AddRef(this)
            ret(++effect.references, 1);
            return;
        }
        case 2: { // Release(this)
            uint32_t refs = --effect.references;
            if (refs == 0) {
                DInputRust::destroyEffect(effect.effectId);
                heaps->free(GuestHeaps::process, effect.table);
                heaps->free(GuestHeaps::process, self);
                s_effectObjects.erase(it);
            }
            ret(refs, 1);
            return;
        }
        case 3: // Initialize(this, hinst, dwVersion, rguid)
            ret(0, 4); // DI_OK
            return;
        case 4: // GetAttributes(this, pei)
            ret(0, 2); // DI_OK
            return;
        case 5: { // SetParameters(this, peff, dwFlags)
            uint32_t peff = arg(1);
            if (peff && memory && memory->readable(peff, 48)) {
                uint32_t duration = read32(peff + 8);
                uint32_t gain = read32(peff + 16);
                uint32_t cbSpecific = read32(peff + 44);
                uint32_t pSpecific = read32(peff + 48);
                int32_t mag = 5000;
                uint32_t period = 100000;
                if (pSpecific && cbSpecific >= 4 && memory->readable(pSpecific, cbSpecific)) {
                    mag = static_cast<int32_t>(read32(pSpecific));
                    if (cbSpecific >= 16) {
                        period = read32(pSpecific + 12);
                    }
                }
                DInputRust::setEffectParams(effect.effectId, duration, gain, mag, period);
            }
            ret(0, 3); // DI_OK
            return;
        }
        case 6: { // Start(this, dwIterations, dwFlags)
            uint32_t iterations = arg(1);
            uint32_t flags = arg(2);
            uint32_t res = DInputRust::startEffect(effect.effectId, iterations, flags);
            ret(res, 3);
            return;
        }
        case 7: { // Stop(this)
            uint32_t res = DInputRust::stopEffect(effect.effectId);
            ret(res, 1);
            return;
        }
        case 8: { // GetEffectStatus(this, pdwFlags)
            uint32_t outFlags = arg(1);
            if (!outFlags) {
                ret(0x80070057u, 2); // DIERR_INVALIDPARAM
                return;
            }
            uint32_t status = 0;
            uint32_t res = DInputRust::getEffectStatus(effect.effectId, &status);
            write32(outFlags, status);
            ret(res, 2);
            return;
        }
        case 9:  // Download(this)
        case 10: // Unload(this)
        case 11: // Escape(this, pesc)
            ret(0, 2); // DI_OK
            return;
        default:
            ret(0, 1);
            return;
    }
}

// Handles IDirectInputDevice8::CreateEffect (Method 18)
inline void dinputDeviceCreateEffect() {
    uint32_t rguid = arg(1);
    uint32_t lpeff = arg(2);
    uint32_t ppdeff = arg(3);

    if (!ppdeff) {
        ret(0x80070057u, 5); // DIERR_INVALIDPARAM
        return;
    }

    uint32_t guidData1 = (rguid && memory && memory->readable(rguid, 4)) ? read32(rguid) : 0x13548632u;
    uint32_t duration = 0xFFFFFFFFu; // Default infinite
    uint32_t gain = 10000;
    int32_t mag = 5000;
    uint32_t period = 100000;

    if (lpeff && memory && memory->readable(lpeff, 48)) {
        duration = read32(lpeff + 8);
        gain = read32(lpeff + 16);
        uint32_t cbSpecific = read32(lpeff + 44);
        uint32_t pSpecific = read32(lpeff + 48);
        if (pSpecific && cbSpecific >= 4 && memory->readable(pSpecific, cbSpecific)) {
            mag = static_cast<int32_t>(read32(pSpecific));
            if (cbSpecific >= 16) {
                period = read32(pSpecific + 12);
            }
        }
    }

    uint32_t rustId = 0;
    uint32_t res = DInputRust::createEffect(guidData1, duration, gain, mag, period, &rustId);
    if (res == 0) {
        uint32_t guestEffect = createGuestInputEffect(rustId);
        write32(ppdeff, guestEffect);
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "DirectInput CreateEffect: guid=%08x id=%u -> obj=%08x",
                        guidData1, rustId, guestEffect);
    }
    ret(res, 5);
}

// Handles IDirectInputDevice8::GetForceFeedbackState (Method 21)
inline void dinputDeviceGetForceFeedbackState() {
    uint32_t pdwOut = arg(1);
    if (!pdwOut) {
        ret(0x80070057u, 2); // DIERR_INVALIDPARAM
        return;
    }
    uint32_t state = 0;
    uint32_t res = DInputRust::getState(&state);
    write32(pdwOut, state);
    ret(res, 2);
}

// Handles IDirectInputDevice8::SendForceFeedbackCommand (Method 22)
inline void dinputDeviceSendForceFeedbackCommand() {
    uint32_t dwFlags = arg(1);
    uint32_t res = DInputRust::sendCommand(dwFlags);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "DirectInput SendForceFeedbackCommand: flags=%u -> res=%u",
                    dwFlags, res);
    ret(res, 2);
}

#endif // GUEST_DINPUT_FEEDBACK_INC
