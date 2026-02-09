#!/usr/bin/env python3
import subprocess
import time
from datetime import datetime, timezone
import threading
import signal
from ctypes import CDLL, Structure, c_uint8, c_double, c_int, POINTER, byref, c_void_p

# ================== CONFIG ==================
CAMERAS = [
    {
        "name": "Kitchen",
        "stream1": "rtsp://username:password@10.101.101.2/stream1",  # HQ recording
        "stream2": "rtsp://username:password@10.101.101.2/stream2",  # LQ motion detect
    },
    {
        "name": "Bedroom",
        "stream1": "rtsp://username:password@10.101.101.3/stream1",  # HQ recording
        "stream2": "rtsp://username:password@10.101.101.3/stream2",  # LQ motion detect
    },
]
BASE_DIR = "/home/dietpi"
WIDTH = 1280
HEIGHT = 720
PIXELS = WIDTH * HEIGHT

EXIT = False

# ---------- SIGNAL HANDLER ----------
def signal_handler(sig, frame):
    global EXIT
    EXIT = True

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

# ---------- load libmotion ----------
libmotion = CDLL("./libmotion.so")

libmotion.motion_new.restype = c_void_p
libmotion.motion_new.argtypes = [c_int]
libmotion.motion_free.argtypes = [c_void_p]
libmotion.motion_feed_next_frame.argtypes = [c_void_p]
libmotion.motion_feed_next_frame.restype = c_int

# ---------- RECORDING PIPELINE ----------
def start_ffmpeg_record(camera_name, rtsp_url, timestamp):
    out_filename = f"{camera_name}_{timestamp}.mkv"

    ffmpeg_cmd = [
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
        "-i", rtsp_url,
        "-c", "copy",
        f"{BASE_DIR}/{camera_name}/{out_filename}"
    ]

    ffmpeg_proc = subprocess.Popen(
        ffmpeg_cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
    )
    ffmpeg_proc.stdout.close()

    return ffmpeg_proc

# ---------- CAMERA THREAD ----------
def camera_loop(camera):
    name = camera["name"]

    ffmpeg_cmd = [
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
        "-i", camera["stream2"],
        "-vf", f"libplacebo=custom_shader_path=frame-diff.glsl",
        "-an",
        "-f", "rawvideo",
        "-pix_fmt", "gray",
        "-"
    ]

    proc = subprocess.Popen(
        ffmpeg_cmd,
        stdout=subprocess.PIPE,
        bufsize=10**8
    )

    fd = proc.stdout.fileno()
    state = libmotion.motion_new(fd)

    recording = False
    ffmpeg_proc = None

    while not EXIT:
        result = libmotion.motion_feed_next_frame(state)

        if result == 1 and not recording:
            ts = datetime.now(timezone.utc).strftime("%Y-%m-%d_%H-%M-%S")
            ffmpeg_proc = start_ffmpeg_record(name, camera["stream1"], ts)
            recording = True
        elif result == -1 and recording:
            if ffmpeg_proc:
                ffmpeg_proc.terminate()
                ffmpeg_proc.wait()
            ffmpeg_proc = None
            recording = False

    # cleanup
    if ffmpeg_proc:
        ffmpeg_proc.terminate()
        ffmpeg_proc.wait()
    libmotion.motion_free(state)

# ---------- MAIN ----------
def main():
    threads = []
    for cam in CAMERAS:
        t = threading.Thread(target=camera_loop, args=(cam,), daemon=True)
        t.start()
        threads.append(t)

    while not EXIT:
        time.sleep(0.5)

    for t in threads:
        t.join()

    print("All cameras exited cleanly")

if __name__ == "__main__":
    main()
