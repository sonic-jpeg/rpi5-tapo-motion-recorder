#ifndef CAMERAS_H
#define CAMERAS_H
#include <stddef.h>

/* Camera definition */
typedef struct {
    char *name;
    char *stream_hq;
    char *stream_lq;
	
    char *output_dir;
	
    int width;
    int height;
    int frame_history;

    double active_threshold;
    int start_frames;
    int prestop_low_min;
    int prestop_low_max;
    int prestop_low_full;
    int prestop_high_max;
    int cancel_prestop_frames;
    double full_stop_delay;
} camera_t;

extern camera_t *CAMERAS;
extern size_t NUM_CAMERAS;

int load_cameras(const char *filename);
void free_cameras(void);


#endif /* CAMERAS_H */
