#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "process.h"
#include "motion.h"     // motion_t + motion_* functions
#include "cameras.h"    // JSON camera loader

/* ================= GLOBAL EXIT ================= */

static volatile sig_atomic_t EXIT = 0;

static void handle_signal(int sig) {
    (void)sig;
    EXIT = 1;
}

/* ================= UTILS ================= */

static void utc_timestamp(char *buf, size_t len) {
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, len, "%Y-%m-%d_%H-%M-%S", &tm);
}

/* ================= RECORDING ================= */

static int start_ffmpeg_record(
    process_t *p,
    const camera_t *cam
) {
    char ts[64];
    utc_timestamp(ts, sizeof(ts));

    char out_path[512];
    snprintf(out_path, sizeof(out_path),
             "%s/%s_%s.mkv",
             cam->output_dir,
             cam->name,
             ts);

    char *argv[] = {
        "ffmpeg",
        "-hide_banner",
        "-loglevel", "quiet",
        "-hwaccel", "drm",
        "-rtsp_transport", "udp",
        "-reorder_queue_size", "4000",
        "-max_delay", "5000000",
        "-timeout", "5000000",
        "-avoid_negative_ts", "make_zero",
        "-seek2any", "1",
        "-fflags", "+genpts",
        "-i", (char *)cam->stream_hq,
        "-c", "copy",
        out_path,
        NULL
    };

    return process_spawn(p, argv, 0, 0, 0);
}


/* ================= CAMERA THREAD ================= */

static void *camera_loop(void *arg) {
    camera_t *cam = arg;

    /* ---- spawn motion ffmpeg ---- */
    char *motion_argv[] = {
        "ffmpeg",
        "-hide_banner",
        "-loglevel", "quiet",
        "-hwaccel", "drm",
        "-rtsp_transport", "udp",
        "-reorder_queue_size", "4000",
        "-max_delay", "3000000",
        "-timeout", "2000000",
        "-avoid_negative_ts", "make_zero",
        "-seek2any", "1",
        "-fflags", "+genpts",
        "-i", (char *)cam->stream_lq,
        "-vf", "libplacebo=custom_shader_path=frame-diff.glsl",
        "-an",
        "-f", "rawvideo",
        "-pix_fmt", "gray",
        "-",
        NULL
    };

    process_t motion_proc;
    if (process_spawn(&motion_proc, motion_argv, 0, 1, 1) != 0) {
        perror("motion ffmpeg spawn failed");
        return NULL;
    }

    /* ---- motion state ---- */
    motion_t *motion = motion_new(motion_proc.stdout_fd, cam);
    if (!motion) {
        process_terminate(&motion_proc);
        process_wait(&motion_proc, NULL);
        process_close(&motion_proc);
        return NULL;
    }

    int recording = 0;
    process_t record_proc;

    while (!EXIT) {
        int r = motion_feed_next_frame(motion);

        if (r == 1 && !recording) {
            if (start_ffmpeg_record(&record_proc, cam) == 0) {
                recording = 1;
            }
        }
        else if (r == -1 && recording) {
            process_terminate(&record_proc);
            process_wait(&record_proc, NULL);
            process_close(&record_proc);
            recording = 0;
        }
    }

    /* ---- cleanup ---- */
    if (recording) {
        process_terminate(&record_proc);
        process_wait(&record_proc, NULL);
        process_close(&record_proc);
    }

    process_terminate(&motion_proc);
    process_wait(&motion_proc, NULL);
    process_close(&motion_proc);

    motion_free(motion);
    return NULL;
}

/* ================= MAIN ================= */

int main(void) {
    /* ---- load cameras from JSON ---- */
    if (load_cameras("cameras.json") != 0) {
        fprintf(stderr, "Failed to load cameras.json\n");
        return 1;
    }

    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pthread_t threads[NUM_CAMERAS];

    for (size_t i = 0; i < NUM_CAMERAS; i++) {
        pthread_create(&threads[i], NULL, camera_loop, &CAMERAS[i]);
    }

    for (size_t i = 0; i < NUM_CAMERAS; i++) {
        pthread_join(threads[i], NULL);
    }

    fprintf(stderr, "All cameras exited cleanly\n");

    free_cameras();
    return 0;
}
