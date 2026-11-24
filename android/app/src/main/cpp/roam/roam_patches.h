#ifndef ROAM_PATCHES_H
#define ROAM_PATCHES_H

#ifdef __cplusplus
extern "C" {
#endif

void hiddenraces_frame_tick(void);
void extraoptions_init_patches(void);
void extraoptions_set_dumb_ai(int dumb_ai);
void extraoptions_frame_tick(void);
void menusize_frame_tick(void);

void garage_frame_tick(void);
void feng_ui_expand_tick(void);
void menu_fixes_tick(void);
void sndlead_frame_tick(void);
void whine_frame_tick(void);
void framerate_tick_base_apply(void);

#ifdef __cplusplus
}
#endif

#endif /* ROAM_PATCHES_H */
