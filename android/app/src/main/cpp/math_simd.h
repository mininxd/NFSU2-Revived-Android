#pragma once
#include <stdint.h>
#include <stddef.h>
#include <dlfcn.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NfsVec3 {
    float x, y, z;
} NfsVec3;

typedef struct NfsAabb {
    NfsVec3 min;
    NfsVec3 max;
} NfsAabb;

typedef struct NfsSuspensionRay {
    NfsVec3 origin;
    NfsVec3 dir;
    float rest_length;
    float spring_k;
    float damper_c;
} NfsSuspensionRay;

typedef struct NfsSuspensionResult {
    int32_t hit;
    float compression;
    float spring_force;
    NfsVec3 contact_normal;
} NfsSuspensionResult;

float nfs_simd_vec3_dot(const NfsVec3* a, const NfsVec3* b);
void nfs_simd_vec3_cross(const NfsVec3* a, const NfsVec3* b, NfsVec3* out);
void nfs_simd_vec3_normalize(const NfsVec3* v, NfsVec3* out);
void nfs_simd_mat4_mul(const float* a, const float* b, float* out);
int32_t nfs_simd_aabb_intersect(const NfsAabb* a, const NfsAabb* b);
void nfs_simd_suspension_step(const NfsSuspensionRay* ray, float ground_height, float wheel_speed, NfsSuspensionResult* out);

// Dynamic wrappers using Rust libgraphics_cache with fast in-place fallbacks
inline float nfs_math_vec3_dot(const NfsVec3* a, const NfsVec3* b) {
    static auto s_fn = reinterpret_cast<float(*)(const NfsVec3*, const NfsVec3*)>(
        dlsym(RTLD_DEFAULT, "nfs_simd_vec3_dot")
    );
    if (s_fn) return s_fn(a, b);
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

inline void nfs_math_mat4_mul(const float* a, const float* b, float* out) {
    static auto s_fn = reinterpret_cast<void(*)(const float*, const float*, float*)>(
        dlsym(RTLD_DEFAULT, "nfs_simd_mat4_mul")
    );
    if (s_fn) {
        s_fn(a, b, out);
        return;
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[i * 4 + k] * b[k * 4 + j];
            out[i * 4 + j] = sum;
        }
    }
}

#ifdef __cplusplus
}
#endif
