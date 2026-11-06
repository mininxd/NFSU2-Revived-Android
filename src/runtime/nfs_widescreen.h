/* nfs_widescreen.h - wide-display HUD/FOV geometry (see widescreen.c). */
#ifndef NFS_WIDESCREEN_H
#define NFS_WIDESCREEN_H
#ifdef __cplusplus
extern "C" {
#endif

/* Enable the fix; reads <game_root>/scripts/NFSUnderground2.WidescreenFix.dat. */
void nfs_widescreen_init(const char* game_root);
/* Screen size: 0 Widescreen (.dat HUD at the edges), 1 = 4:3 (patch off),
 * 2 = Corrected HUD (wide 3D, .dat HUD in a 4:3 centre). */
void nfs_widescreen_set_screen_mode(int mode);
/* The game's render size; call before its HUD draws and on every change. */
void nfs_widescreen_set_resolution(unsigned width, unsigned height);
/* HUD offset entries loaded from the .dat (0: elements keep 4:3 anchors). */
int nfs_widescreen_hud_entries(void);
/* Vertical offset (480-line HUD units, negative = up) added to the minimap. */
void nfs_widescreen_set_minimap_offset(float dy);
/* Reposition bank money GUI to left and offset SMS / Navigation GUI downwards. */
void nfs_widescreen_set_bank_left(int enabled);
void nfs_widescreen_set_sms_nav_offset(float dy);
/* Clamp HUD elements to 16:9 safe bounds on wider displays (20:9, 21:9) */
void nfs_widescreen_set_hud_safe_area(int enabled);
/* Draw distance: maximum camera FarZ (meters) for 3D views (view 1 and road
 * reflection 4). 0 keeps the game's unclamped distance. */
void nfs_widescreen_set_draw_distance(float distance);
/* Game frame pacing (the game caps itself at 60): fps >= 30 replaces its 1/60 s
 * frame time; 0 keeps the original. apply() re-asserts it once per frame,
 * since the game re-sets 1/60 on mode changes. */
void nfs_widescreen_set_frame_rate(unsigned fps);
void nfs_widescreen_apply_frame_rate(void);

#ifdef __cplusplus
}
#endif
#endif
