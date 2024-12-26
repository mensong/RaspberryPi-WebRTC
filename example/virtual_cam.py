from picamera2 import Picamera2, MappedArray
import cv2
import time
import subprocess

width = 1280
height = 720
video_nr = 9
virtual_camera = f"/dev/video{video_nr}"

picam2 = Picamera2()
config = picam2.create_preview_configuration(main={"size": (width, height)})
picam2.configure(config)

ffmpeg_cmd = [
    "ffmpeg",
    "-f",
    "rawvideo",
    "-pix_fmt",
    "yuv420p",
    "-s",
    f"{width}x{height}",
    "-i",
    "-",
    "-f",
    "v4l2",
    virtual_camera,
]
ffmpeg_process = subprocess.Popen(ffmpeg_cmd, stdin=subprocess.PIPE)


def reset_v4l2loopback():
    try:
        print("Resetting v4l2loopback...")
        subprocess.run(["sudo", "rmmod", "v4l2loopback"], check=True)
        subprocess.run(
            [
                "sudo",
                "modprobe",
                "v4l2loopback",
                "devices=1",
                f"video_nr={video_nr}",
                "card_label=ProcessedCam",
                "max_buffers=4",
                "exclusive_caps=1",
            ],
            check=True,
        )
        print("v4l2loopback reset successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Error resetting v4l2loopback: {e}")


def process_frame(request):

    timestamp = time.strftime("%Y-%m-%d %X")

    with MappedArray(request, "main") as m:
        frame = m.array

        font = cv2.FONT_HERSHEY_SIMPLEX
        font_scale = 1
        color = (0, 255, 0)
        thickness = 2
        position = (10, 30)
        cv2.putText(frame, timestamp, position, font, font_scale, color, thickness)
        yuv_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2YUV_I420)

        try:
            ffmpeg_process.stdin.write(yuv_frame.tobytes())
        except BrokenPipeError:
            print("ffmpeg pipe broken. Stopping frame processing.")
            exit()


try:
    reset_v4l2loopback()

    print(f"Streaming to {virtual_camera}...")
    picam2.pre_callback = process_frame
    picam2.start()

    while True:
        time.sleep(1)

except KeyboardInterrupt:
    print("Stopping stream...")

finally:
    picam2.stop()
    if ffmpeg_process.stdin:
        ffmpeg_process.stdin.close()
    ffmpeg_process.terminate()
    ffmpeg_process.wait()
    print("Stream stopped.")
