# Motion Detection Pipeline

This repository contains a proof of concept hardware-accelerated motion detection and recording pipeline, optimized for the Raspberry Pi 5 and RTSP security cameras (e.g. Tapo). The pipeline is configured via JSON.

---

## Overview

The pipeline consists of three stages:

1. **FFmpeg Motion Feed**
   - Reads the low-quality RTSP stream
   - Applies a libplacebo shader:
     - Grayscale conversion
     - Gaussian blur
     - Frame-to-frame difference
     - Encodes per-pixel motion into a grayscale mask
   - Outputs raw frames (`-pix_fmt gray`) to stdout

2. **C Motion State Machine**
   - Reads raw frames via pipe (FD)
   - Computes normalized frame deltas
   - Maintains a rolling delta history
   - Implements START / KEEP / STOP logic

3. **Recording Trigger**
   - On motion START, records the high-quality stream
   - Stops recording after motion subsides using prestop and delay logic

---

## JSON Camera Configuration

Cameras are defined in `cameras.json`. Each camera may override motion parameters and output directory.

### Example `cameras.json`

```json
[
  {
    "name": "Kitchen",
    "stream_hq": "rtsp://username:password@10.101.101.17/stream1",
    "stream_lq": "rtsp://username:password@10.101.101.17/stream2",
    "output_dir": "/mnt/storage/Kitchen",

    "width": 1280,
    "height": 720,

    "frame_history": 10,
    "active_threshold": 0.002,

    "start_frames": 15,
    "prestop_low_min": 10,
    "prestop_low_max": 19,
    "prestop_low_full": 20,
    "prestop_high_max": 20,
    "cancel_prestop_frames": 15,
    "full_stop_delay": 4.0
  },
  {
    "name": "Garage",
    "stream_hq": "rtsp://username:password@10.101.101.18/stream1",
    "stream_lq": "rtsp://username:password@10.101.101.18/stream2",
    "output_dir": "/mnt/storage/Garage"
  }
]
```

All fields have sensible defaults if not provided.

---

## Motion State Machine

The motion logic is implemented in pure C using a `motion_t` struct. It tracks deltas between consecutive frames and triggers recording accordingly.

### `motion_t` Structure

| Field | Type | Description |
|------|------|-------------|
| `fd` | `int` | FD from FFmpeg pipe for reading motion frames |
| `width` | `size_t` | Frame width |
| `height` | `size_t` | Frame height |
| `pixels` | `size_t` | Number of pixels per frame |
| `prev_alpha` | `uint8_t*` | Previous frame pixels |
| `curr_alpha` | `uint8_t*` | Current frame pixels |
| `frame_buf` | `uint8_t*` | Temporary buffer for reading frames |
| `delta_history` | `double*` | Circular buffer of frame deltas |
| `hist_len` | `size_t` | Number of deltas stored |
| `hist_i` | `size_t` | Current delta index |
| `motion_active` | `int` | Motion active flag |
| `hi_run` | `int` | Consecutive frames above threshold |
| `lo_run` | `int` | Consecutive frames below threshold |
| `mid_hi_run` | `int` | High frames after mid-low sequence |
| `mid_lo_seen` | `int` | Mid-low sequence detected |
| `prestop_ts` | `double` | Prestop timer timestamp |

---

## Motion Parameters

| Parameter | Default | Description |
|---------|---------|-------------|
| `width` | 1280 | Frame width |
| `height` | 720 | Frame height |
| `frame_history` | 10 | Frames used for delta averaging |
| `active_threshold` | 0.002 | Minimum delta to detect motion |
| `start_frames` | 15 | Frames above threshold to START |
| `prestop_low_min` | 10 | Min low frames before prestop |
| `prestop_low_max` | 19 | Max low frames before prestop |
| `prestop_low_full` | 20 | Frames to enter prestop timer |
| `prestop_high_max` | 20 | Max highs after mid-low |
| `cancel_prestop_frames` | 15 | Frames to cancel prestop |
| `full_stop_delay` | 4.0 | Seconds before STOP |

---

## Motion API

| Function | Description |
|--------|-------------|
| `motion_new(int fd, const motion_cfg_t *cfg)` | Create motion state |
| `motion_free(motion_t *m)` | Free motion state |
| `motion_feed(motion_t *m, const uint8_t *frame)` | Feed a frame |
| `motion_feed_next_frame(motion_t *m)` | Read + feed next frame |
| `delta_neon(a, b)` | NEON-accelerated delta |
| `delta_no_neon(a, b)` | Scalar delta |

Return values from `motion_feed`:
- `1` → START recording
- `0` → KEEP recording
- `-1` → STOP recording

---

## Build & Run

```sh
make
./motion-recorder
```

Each camera runs in its own thread, with independent motion detection and recording.
