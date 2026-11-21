// Android sound output through AAudio (see AndroidSound.h).

#include "AndroidSound.h"

#include "RealmsAndroid.h"

#include <aaudio/AAudio.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace android_sound {
namespace {

ekron::audio::SoundSystem &instance() {
    // Never destroyed: the AAudio callback may still run while the process exits.
    static auto *sound = new ekron::audio::SoundSystem();
    return *sound;
}

std::atomic<bool> gStarted{false};
std::atomic<bool> gWanted{true}; // false while the activity is paused
std::mutex gStreamMutex;
AAudioStream *gStream = nullptr;
AAssetManager *gAssets = nullptr;

aaudio_data_callback_result_t dataCallback(AAudioStream *, void *user, void *audioData, int32_t numFrames) {
    static_cast<ekron::audio::Mixer *>(user)->render(static_cast<float *>(audioData), numFrames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void openStreamLocked();

void errorCallback(AAudioStream *, void *, aaudio_result_t error) {
    // Device change / disconnect: reopen, but never from the callback thread.
    __android_log_print(ANDROID_LOG_INFO, "Realms", "audio: stream error %s, reopening",
                        AAudio_convertResultToText(error));
    std::thread([]() {
        std::lock_guard<std::mutex> lock(gStreamMutex);
        if (gStream) {
            AAudioStream_requestStop(gStream);
            AAudioStream_close(gStream);
            gStream = nullptr;
        }
        if (gWanted.load()) openStreamLocked();
    }).detach();
}

void openStreamLocked() {
    if (gStream) return;
    AAudioStreamBuilder *builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return;
    auto &mixer = instance().mixer();
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_SONIFICATION);
    AAudioStreamBuilder_setDataCallback(builder, dataCallback, &mixer);
    AAudioStreamBuilder_setErrorCallback(builder, errorCallback, nullptr);
    AAudioStream *stream = nullptr;
    const aaudio_result_t r = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (r != AAUDIO_OK || !stream) {
        __android_log_print(ANDROID_LOG_WARN, "Realms", "audio: cannot open AAudio stream: %s",
                            AAudio_convertResultToText(r));
        return;
    }
    if (AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_FLOAT || AAudioStream_getChannelCount(stream) != 2) {
        __android_log_print(ANDROID_LOG_WARN, "Realms", "audio: device refused float stereo");
        AAudioStream_close(stream);
        return;
    }
    mixer.setOutputRate(AAudioStream_getSampleRate(stream));
    // Two bursts of buffering: low latency without glitches.
    AAudioStream_setBufferSizeInFrames(stream, AAudioStream_getFramesPerBurst(stream) * 2);
    if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
        AAudioStream_close(stream);
        return;
    }
    gStream = stream;
    __android_log_print(ANDROID_LOG_INFO, "Realms", "audio: AAudio %d Hz, burst %d", AAudioStream_getSampleRate(stream),
                        AAudioStream_getFramesPerBurst(stream));
}

void closeStream() {
    std::lock_guard<std::mutex> lock(gStreamMutex);
    if (!gStream) return;
    AAudioStream_requestStop(gStream);
    AAudioStream_close(gStream);
    gStream = nullptr;
}

bool readSound(const std::string &path, std::vector<uint8_t> &out) {
    if (!gAssets) return false;
    AAsset *asset = AAssetManager_open(gAssets, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset) return false;
    const off64_t size = AAsset_getLength64(asset);
    bool ok = size > 0 && size < (off64_t(1) << 28);
    if (ok) {
        out.resize(size_t(size));
        ok = AAsset_read(asset, out.data(), size_t(size)) == size;
    }
    AAsset_close(asset);
    return ok;
}

} // namespace

ekron::audio::SoundSystem &start(android_app *app) {
    auto &sound = instance();
    if (gStarted.load() || !app || !app->activity) return sound;
    gAssets = app->activity->assetManager;
    sound.loadAsync(readSound);
    auto &engine = realms_android::ensureEngine(app);
    sound.registerCommands(engine);
    sound.syncCvars(engine);
    {
        std::lock_guard<std::mutex> lock(gStreamMutex);
        if (gWanted.load()) openStreamLocked();
    }
    gStarted.store(true);
    return sound;
}

ekron::audio::SoundSystem *get() { return gStarted.load() ? &instance() : nullptr; }

void resume() {
    gWanted.store(true);
    if (!gStarted.load()) return;
    std::lock_guard<std::mutex> lock(gStreamMutex);
    openStreamLocked();
}

void pause() {
    gWanted.store(false);
    closeStream();
}

} // namespace android_sound

// Menu sounds from the Kotlin UI: 0 click, 1 open, 2 close, 3 confirm, 4 error.
extern "C" JNIEXPORT void JNICALL Java_com_example_realms_MainActivity_nativeUiSound(JNIEnv *, jobject, jint id) {
    static const char *const names[] = {"ui_click", "ui_open", "ui_close", "ui_confirm", "ui_error"};
    if (id < 0 || id > 4) return;
    if (auto *sound = android_sound::get()) sound->ui(names[id]);
}
