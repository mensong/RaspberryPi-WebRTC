#include "args.h"
#include "capturer/v4l2_capturer.h"
#include "recorder/recorder_manager.h"

int main(int argc, char *argv[]) {
    Args args{.fps = 15,
              .width = 1280,
              .height = 720,
              .sample_rate = 48000,
              .hw_accel = true,
              .format = V4L2_PIX_FMT_H264,
              .record_path = "./"};

    auto video_capture = V4L2Capturer::Create(args);
    auto audio_capture = PaCapturer::Create(args);
    auto recorder_mgr = RecorderManager::Create(video_capture, audio_capture, args);
    sleep(45);

    return 0;
}
