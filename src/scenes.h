/* tinydaw: the full-screen show's scenes besides the blobs. Sacred geometry (from geoshade) and
 * shaders (from termshade), drawn into a float RGB image at whatever size the show asks for. */
#ifndef SCENES_H
#define SCENES_H

#include <stddef.h>

enum { SCENE_GEOMETRY_FIRST = 0, SCENE_GEOMETRY_COUNT = 8, SCENE_SHADER_FIRST = 8, SCENE_COUNT = 16 };

int scene_count(void);
const char *scene_name(int i);
const char *scene_caption(int i);
/* the render width that keeps scene i smooth for a window this wide (heavy shaders draw smaller
 * and are scaled up smoothly) */
int scene_width(int i, int window_w);
/* Draws scene i at t seconds into rgb (w * h * 3 floats, 0..1). zoom > 1 magnifies (the bass makes
 * the picture breathe). dt is the time since its last frame, for the scenes that keep state (fire,
 * stars). caption gets a live detail such as which Platonic solid is showing, or "". */
void scene_render(int i, double t, double dt, float zoom, float *rgb, int w, int h, char *caption, size_t n);
void scene_reset(int i);              /* forget a scene's state, so it starts afresh */

#endif
