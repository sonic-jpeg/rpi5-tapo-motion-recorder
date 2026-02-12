#include "cameras.h"
#include <jansson.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Global variables */
camera_t *CAMERAS = NULL;
size_t NUM_CAMERAS = 0;

/* ---- helpers for defaults ---- */
#define JINT(o,k,d) \
    (json_object_get(o,k) && json_is_integer(json_object_get(o,k)) ? \
     (int)json_integer_value(json_object_get(o,k)) : (d))

#define JDBL(o,k,d) \
    (json_object_get(o,k) && json_is_number(json_object_get(o,k)) ? \
     json_number_value(json_object_get(o,k)) : (d))

#define JSTR(o,k) \
    (json_object_get(o,k) && json_is_string(json_object_get(o,k)) ? \
     json_string_value(json_object_get(o,k)) : NULL)

int load_cameras(const char *filename) {
    json_error_t error;
    json_t *root = json_load_file(filename, 0, &error);
    if (!root) {
        fprintf(stderr, "Failed to load %s: %s\n", filename, error.text);
        return -1;
    }

    if (!json_is_array(root)) {
        fprintf(stderr, "%s is not a JSON array\n", filename);
        json_decref(root);
        return -1;
    }

    NUM_CAMERAS = json_array_size(root);
    CAMERAS = calloc(NUM_CAMERAS, sizeof(camera_t));
    if (!CAMERAS) {
        perror("calloc");
        json_decref(root);
        return -1;
    }

    for (size_t i = 0; i < NUM_CAMERAS; i++) {
        json_t *item = json_array_get(root, i);

        const char *name = JSTR(item, "name");
        const char *hq   = JSTR(item, "stream_hq");
        const char *lq   = JSTR(item, "stream_lq");
		const char *out = JSTR(item, "output_dir");

        if (!name || !hq || !lq || !out) {
            fprintf(stderr, "Invalid camera entry at index %zu\n", i);
            free_cameras();
            json_decref(root);
            return -1;
        }

        camera_t *c = &CAMERAS[i];

        /* ---- strings ---- */
        c->name      = strdup(name);
        c->stream_hq = strdup(hq);
        c->stream_lq = strdup(lq);
        c->output_dir = strdup(out);
		
        /* ---- video geometry ---- */
        c->width  = JINT(item, "width", 1280);
        c->height = JINT(item, "height", 720);

        /* ---- motion config ---- */
        c->frame_history        = JINT(item, "frame_history", 10);
        c->active_threshold     = JDBL(item, "active_threshold", 0.002);

        c->start_frames         = JINT(item, "start_frames", 15);
        c->prestop_low_min      = JINT(item, "prestop_low_min", 10);
        c->prestop_low_max      = JINT(item, "prestop_low_max", 19);
        c->prestop_low_full     = JINT(item, "prestop_low_full", 20);
        c->prestop_high_max     = JINT(item, "prestop_high_max", 20);
        c->cancel_prestop_frames= JINT(item, "cancel_prestop_frames", 15);
        c->full_stop_delay      = JDBL(item, "full_stop_delay", 4.0);
    }

    json_decref(root);
    return 0;
}

void free_cameras(void) {
    if (!CAMERAS) return;

    for (size_t i = 0; i < NUM_CAMERAS; i++) {
        free(CAMERAS[i].name);
        free(CAMERAS[i].stream_hq);
        free(CAMERAS[i].stream_lq);
		free(CAMERAS[i].output_dir);
    }

    free(CAMERAS);
    CAMERAS = NULL;
    NUM_CAMERAS = 0;
}
