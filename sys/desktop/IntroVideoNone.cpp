// No intro video backend in this build (see IntroVideo.h): the game starts
// straight away. Linux builds get one by installing FFmpeg's development
// packages (libavformat-dev libavcodec-dev libswscale-dev libswresample-dev).

#include "IntroVideo.h"

namespace ekron::desktop {

const char *introVideoBackend() { return "none"; }

std::unique_ptr<IntroVideo> openIntroVideo(const std::string &, const IntroOptions &, std::string &error) {
    error = "built without a video decoder (FFmpeg development files were not found)";
    return nullptr;
}

} // namespace ekron::desktop
