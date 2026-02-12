#ifndef MOTION_H
#define MOTION_H
#include <stdint.h>
#include <stddef.h>

#include "cameras.h"

typedef struct {
    int fd;
    const camera_t *cam;

    size_t pixels;

    /* frame buffers */
    uint8_t *prev_motion_mask;
    uint8_t *curr_motion_mask;
    uint8_t *frame_buf;

    /* history */
    size_t frame_history_len;
    double *delta_history;
    size_t hist_i;
    size_t hist_len;

    /* state */
    int motion_active;
    int hi_run;
    int lo_run;
    int mid_hi_run;
    int mid_lo_seen;
    double prestop_ts;
} motion_t;

/* return 0 on success, -1 on error */
int generate_motion_shader_glsl(
    float sigma,
    int radius,
    float motion_threshold,
    int width,
    int height,
    char *out_path,
    size_t out_path_len
);
motion_t *motion_new(int fd, const camera_t *cam);
void motion_free(motion_t *m);

int motion_feed_next_frame(motion_t *m);
double motion_last_avg(motion_t *m);
#endif /* MOTION_H */