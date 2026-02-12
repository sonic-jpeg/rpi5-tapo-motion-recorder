#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arm_neon.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "motion.h"


/* return 0 on success, -1 on error */
int generate_motion_shader_glsl(
    float sigma,
    int radius,
    float motion_threshold,
    int width,
    int height,
    char *out_path,
    size_t out_path_len
) {
    /* ---- build deterministic filename ---- */
    snprintf(
        out_path,
        out_path_len,
        "motion_s%.2f_r%d_t%.4f_%dx%d.glsl",
        sigma,
        radius,
        motion_threshold,
        width,
        height
    );

    /* ---- if file already exists, reuse ---- */
    struct stat st;
    if (stat(out_path, &st) == 0) {
        return 0;
    }

    /* ---- open for write ---- */
    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "fopen(%s) failed: %s\n", out_path, strerror(errno));
        return -1;
    }

    /* ---- write shader ---- */
    fprintf(f,
        "//!HOOK MAIN\n"
        "//!BIND HOOKED\n"
        "//!BIND PREV\n"
        "//!DESC grayscale -> blur -> motion detect\n"
        "\n"
        "////////////////////////////////////////////////////////////////////////\n"
        "// AUTO-GENERATED FILE — DO NOT EDIT\n"
        "// sigma=%.3f radius=%d threshold=%.6f size=%dx%d\n"
        "////////////////////////////////////////////////////////////////////////\n"
        "\n"
        "#define SIGMA %.6f\n"
        "#define RADIUS %d.0\n"
        "#define MOTION_THRESHOLD %.6f\n"
        "\n"
        "#define get_weight(x) (exp(-(x)*(x)/(2.0*SIGMA*SIGMA)))\n"
        "\n"
        "vec4 hook() {\n"
        "    vec4 curr = linearize(textureLod(HOOKED_raw, HOOKED_pos, 0.0) * HOOKED_mul);\n"
        "\n"
        "    float gray = dot(curr.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
        "    vec4 gray_vec = vec4(gray, gray, gray, 0.0);\n"
        "\n"
        "    vec4 csum = gray_vec;\n"
        "    float wsum = 1.0;\n"
        "    for (float i = 1.0; i <= RADIUS; ++i) {\n"
        "        float w = get_weight(i);\n"
        "        csum += (textureLod(HOOKED_raw, HOOKED_pos + vec2(0.0, -i)/HOOKED_size.xy, 0.0)\n"
        "              + textureLod(HOOKED_raw, HOOKED_pos + vec2(0.0,  i)/HOOKED_size.xy, 0.0)) * w;\n"
        "        wsum += 2.0 * w;\n"
        "    }\n"
        "    vec4 blur_y = csum / wsum;\n"
        "\n"
        "    csum = blur_y;\n"
        "    wsum = 1.0;\n"
        "    for (float i = 1.0; i <= RADIUS; ++i) {\n"
        "        float w = get_weight(i);\n"
        "        csum += (textureLod(HOOKED_raw, HOOKED_pos + vec2(-i, 0.0)/HOOKED_size.xy, 0.0)\n"
        "              + textureLod(HOOKED_raw, HOOKED_pos + vec2( i, 0.0)/HOOKED_size.xy, 0.0)) * w;\n"
        "        wsum += 2.0 * w;\n"
        "    }\n"
        "    vec4 blur = csum / wsum;\n"
        "\n"
        "    ivec3 pos = ivec3(HOOKED_pos * HOOKED_size, 0);\n"
        "    vec4 prev = imageLoad(PREV, pos);\n"
        "    float diff = abs(blur.r - prev.r);\n"
        "    float motion = diff > MOTION_THRESHOLD ? 1.0 : 0.0;\n"
        "\n"
        "    imageStore(PREV, pos, blur);\n"
        "\n"
        "    return vec4(motion);\n"
        "}\n"
        "\n"
        "//!TEXTURE PREV\n"
        "//!SIZE %d %d 1\n"
        "//!FORMAT r8\n"
        "//!STORAGE\n",
        sigma, radius, motion_threshold, width, height,
        sigma,
        radius,
        motion_threshold,
        width,
        height
    );

    fclose(f);
    return 0;
}


/* ================= TIME ================= */

static inline double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

double motion_last_avg(motion_t *m) {
    if (m->hist_len == 0) return 0.0;
    return m->delta_history[
        (m->hist_i - 1 + m->frame_history_len) % m->frame_history_len
    ];
}

/* ================= NEON DELTA ================= */

static inline double delta_neon(const uint8_t *a,
                               const uint8_t *b,
                               size_t pixels)
{
    uint32x4_t acc = vdupq_n_u32(0);
    size_t i = 0;

    for (; i + 16 <= pixels; i += 16) {
        uint8x16_t va = vld1q_u8(a + i);
        uint8x16_t vb = vld1q_u8(b + i);
        uint8x16_t vd = vabdq_u8(va, vb);

        acc = vpadalq_u16(acc, vmovl_u8(vget_low_u8(vd)));
        acc = vpadalq_u16(acc, vmovl_u8(vget_high_u8(vd)));
    }

    uint64_t sum =
        (uint64_t)vgetq_lane_u32(acc, 0) +
        (uint64_t)vgetq_lane_u32(acc, 1) +
        (uint64_t)vgetq_lane_u32(acc, 2) +
        (uint64_t)vgetq_lane_u32(acc, 3);

    for (; i < pixels; i++)
        sum += abs(a[i] - b[i]);

    return (double)sum / (pixels * 255.0);
}

/* ================= LIFECYCLE ================= */

motion_t *motion_new(int fd, const camera_t *cam) {
    motion_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;

    m->fd  = fd;
    m->cam = cam;

    m->pixels = (size_t)cam->width * cam->height;
    m->frame_history_len = cam->frame_history;

    m->delta_history = calloc(m->frame_history_len, sizeof(double));
    m->prev_motion_mask = aligned_alloc(16, m->pixels);
    m->curr_motion_mask = aligned_alloc(16, m->pixels);
    m->frame_buf  = malloc(m->pixels);

    if (!m->delta_history || !m->prev_motion_mask ||
        !m->curr_motion_mask || !m->frame_buf)
    {
        motion_free(m);
        return NULL;
    }

    return m;
}

void motion_free(motion_t *m) {
    if (!m) return;
    free(m->prev_motion_mask);
    free(m->curr_motion_mask);
    free(m->frame_buf);
    free(m->delta_history);
    free(m);
}

/* ================= CORE ================= */

static int motion_feed(motion_t *m, const uint8_t *a) {
    const camera_t *c = m->cam;

    memcpy(m->curr_motion_mask, a, m->pixels);

    double delta = delta_neon(
        m->curr_motion_mask,
        m->prev_motion_mask,
        m->pixels
    );

    memcpy(m->prev_motion_mask, m->curr_motion_mask, m->pixels);

    m->delta_history[m->hist_i++ % m->frame_history_len] = delta;
    if (m->hist_len < m->frame_history_len)
        m->hist_len++;

    double avg = 0.0;
    for (size_t i = 0; i < m->hist_len; i++)
        avg += m->delta_history[i];
    avg /= m->hist_len;

    /* ---------- START ---------- */
    if (!m->motion_active) {
        if (avg > c->active_threshold)
            m->hi_run++;
        else
            m->hi_run = 0;

        if (m->hi_run >= c->start_frames) {
            m->motion_active = 1;
            m->hi_run = m->lo_run = m->mid_hi_run = m->mid_lo_seen = 0;
            m->prestop_ts = 0.0;
            return 1;
        }
        return 0;
    }

    /* ---------- RECORDING ---------- */
    if (m->prestop_ts == 0.0) {
        if (avg <= c->active_threshold) {
            if (++m->lo_run >= c->prestop_low_full)
                m->prestop_ts = now_sec();
        } else {
            m->lo_run = 0;
        }
        return 0;
    }

    /* ---------- CANCEL PRESTOP ---------- */
    if (avg > c->active_threshold) {
        if (++m->hi_run >= c->cancel_prestop_frames) {
            m->prestop_ts = 0.0;
            m->hi_run = m->lo_run = 0;
        }
        return 0;
    }

    /* ---------- FULL STOP ---------- */
    if (now_sec() - m->prestop_ts >= c->full_stop_delay) {
        m->motion_active = 0;
        m->prestop_ts = 0.0;
        return -1;
    }

    return 0;
}

int motion_feed_next_frame(motion_t *m) {
    size_t got = 0;

    while (got < m->pixels) {
        ssize_t n = read(m->fd, m->frame_buf + got, m->pixels - got);
        if (n <= 0)
            return 0;
        got += n;
    }

    return motion_feed(m, m->frame_buf);
}
