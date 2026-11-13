#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Install runtime compatibility hooks into the prebuilt libnfsu2_android.so.
 * Safe to call more than once. Must run after that library is loaded. */
void roam_compat_install(void);
void roam_compat_apply_hud(void);

#ifdef __cplusplus
}
#endif
