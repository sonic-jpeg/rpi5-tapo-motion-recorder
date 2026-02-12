#ifndef CAMERAS_H
#define CAMERAS_H

#include <stddef.h>

/* Camera definition */
typedef struct {
    char *name;
    char *stream_hq;
    char *stream_lq;

    char *output_dir;

    /* ---- video geometry ---- */
    int width;
    int height;

    /* ---- motion history ---- */
    int frame_history;

    /* ---- motion thresholds ---- */
    double active_threshold;
    int start_frames;
    int prestop_low_min;
    int prestop_low_max;
    int prestop_low_full;
    int prestop_high_max;
    int cancel_prestop_frames;
    double full_stop_delay;

    /* ---- shader parameters ---- */
    double sigma;
    int radius;
    double motion_threshold;

} camera_t;

extern camera_t *CAMERAS;
extern size_t NUM_CAMERAS;

int load_cameras(const char *filename);
void free_cameras(void);

#endif /* CAMERAS_H */
