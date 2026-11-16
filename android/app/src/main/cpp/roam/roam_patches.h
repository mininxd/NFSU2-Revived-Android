#ifndef ROAM_PATCHES_H
#define ROAM_PATCHES_H

#ifdef __cplusplus
extern "C" {
#endif

void hiddenraces_frame_tick(void);
void extraoptions_init_patches(void);
void extraoptions_frame_tick(int dumb_ai);
void menusize_frame_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* ROAM_PATCHES_H */
