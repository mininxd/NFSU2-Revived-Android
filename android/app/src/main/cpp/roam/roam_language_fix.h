#ifndef ROAM_LANGUAGE_FIX_H
#define ROAM_LANGUAGE_FIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * roam_language_fix.h - Dedicated Spanish & Multilingual Subsystem for NFSU2 Android (Issue #112).
 * Sizing LanguageMemoryPool to support full localization files (>273KB) including Spanish (280KB).
 */
void roam_language_fix_init(void);
void roam_language_fix_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* ROAM_LANGUAGE_FIX_H */
