# Motion Detection Pipeline

This repository contains a hardware accelerated motion detection and recording pipeline, for Tapo security cameras and optimized for the Raspberry Pi 5

---

## Overview

The pipeline:

1. **FFmpeg** reads the low-quality RTSP stream and applies a custom libplacebo shader:
   - Converts frames to grayscale
   - Applies Gaussian blur
   - Computes diff of current and previous pixel
   - Compares diff against a threadhold
   - Encodes all results into a "motion frame"
2. **C Motion State Machine**:
   - Reads raw motion frames via pipe (FD)
   - Computes frame-to-frame deltas
   - Tracks motion state (START / KEEP / STOP)
   - Uses delta history for smoothing
3. **Recording Trigger**:
   - When motion is detected, a high-quality stream is recorded
   - Recording stops after motion subsides, with prestop and delay logic to avoid flicker

---

## Motion State Machine

### `motion_t` Structure

| Field | Type | Description |
|-------|------|-------------|
| `prev_motion_mask` | `uint8_t*` | Previous frame pixels; used for delta calculation |
| `curr_motion_mask` | `uint8_t*` | Current frame pixels |
| `delta_history` | `double[FRAME_HISTORY]` | Circular buffer of recent frame deltas |
| `hist_len` | `int` | Number of deltas stored (≤ `FRAME_HISTORY`) |
| `hist_i` | `int` | Next index for delta circular buffer |
| `motion_active` | `int` | 1 if motion recording is active, 0 otherwise |
| `hi_run` | `int` | Consecutive frames above threshold (START / cancel prestop) |
| `lo_run` | `int` | Consecutive frames below threshold (prestop logic) |
| `mid_hi_run` | `int` | High frames after mid-low sequence; used to prevent premature STOP |
| `mid_lo_seen` | `int` | Flag indicating a mid-low sequence was detected |
| `prestop_ts` | `double` | Timestamp when prestop timer started |
| `frame_buf` | `uint8_t*` | Temporary buffer for reading raw frames |
| `fd` | `int8_t` | FD from FFmpeg pipe for reading motion frames |

---

### Constants / Parameters

| Constant | Value | Purpose |
|----------|-------|---------|
| `WIDTH` | 1280 | Frame width |
| `HEIGHT` | 720 | Frame height |
| `PIXELS` | `WIDTH * HEIGHT` | Number of pixels per frame |
| `FRAME_HISTORY` | 10 | Number of frame deltas to keep for averaging |
| `ACTIVE_THRESHOLD` | 0.002 | Min average delta to detect motion |
| `START_FRAMES` | 15 | Frames above threshold to trigger START |
| `PRESTOP_LOW_MIN` | 10 | Minimum frames below threshold for prestop |
| `PRESTOP_LOW_MAX` | 19 | Maximum frames below threshold for prestop |
| `PRESTOP_LOW_FULL` | 20 | Frames below threshold to enter prestop timer |
| `PRESTOP_HIGH_MAX` | 20 | Max frames above threshold after mid-low sequence |
| `CANCEL_PRESTOP_FRAMES` | 15 | Frames above threshold to cancel prestop |
| `FULL_STOP_DELAY` | 4.0 | Seconds to wait before full STOP |

---

### Functions

- **`motion_new(int8_t fd)`**  
  Allocate and initialize a `motion_t` struct and frame buffers.

- **`motion_free(motion_t *m)`**  
  Free allocated memory.

- **`delta_no_neon(const uint8_t *a, const uint8_t *b)`**  
  Compute normalized frame delta without SIMD.

- **`delta_neon(const uint8_t *a, const uint8_t *b)`**  
  Compute normalized frame delta using ARM NEON SIMD (faster).

- **`motion_feed(motion_t *m, const uint8_t *a)`**  
  Feed a single frame into the state machine:
  - Returns `1` → START recording
  - Returns `0` → KEEP recording
  - Returns `-1` → STOP recording

- **`motion_feed_next_frame(motion_t *m)`**  
  Reads a full frame from the FD and calls `motion_feed`.

---
