#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Android Dynamic Performance Framework (ADPF) manager for NFSU2 Revived.
 * Communicates target frame work duration to the Android PowerHAL / kernel governor
 * on Android 12+ (API 31+ / HyperOS), preventing thermal throttling dips and hitching.
 */

// Initializes ADPF session for the calling render thread.
void nfs_adpf_init(int target_fps);

// Marks the start of a frame work cycle.
void nfs_adpf_begin_frame(void);

// Reports the frame duration to PowerHAL after presentation.
void nfs_adpf_end_frame(void);

// Destroys active ADPF session.
void nfs_adpf_destroy(void);

#ifdef __cplusplus
}
#endif
