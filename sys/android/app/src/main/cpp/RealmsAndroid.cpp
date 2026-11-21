#include "RealmsAndroid.h"

#include "realms/SandboxBoot.h"

#include <android/asset_manager.h>
#include <android/log.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace realms_android {
namespace {

realms::Engine gEngine;
AAssetManager *gAssets = nullptr;
std::string gInternal;

bool safeRelative(const std::string &path) {
    return !path.empty() && path[0] != '/' && path.find("..") == std::string::npos;
}

bool readInternal(const std::string &path, std::vector<unsigned char> &out) {
    if (gInternal.empty()) return false;
    std::ifstream f(gInternal + "/" + path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool readAsset(const std::string &path, std::vector<unsigned char> &out) {
    if (!gAssets) return false;
    AAsset *asset = AAssetManager_open(gAssets, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset) return false;
    const off64_t size = AAsset_getLength64(asset);
    bool ok = size >= 0 && size < (off64_t(1) << 31);
    if (ok) {
        out.resize(size_t(size));
        ok = size == 0 || AAsset_read(asset, out.data(), size_t(size)) == size;
    }
    AAsset_close(asset);
    return ok;
}

int32_t readAssetRange(const std::string &path, int64_t offset, int32_t size, void *outBuffer) {
    if (!gInternal.empty()) {
        std::ifstream f(gInternal + "/" + path, std::ios::binary);
        if (f) {
            f.seekg(offset, std::ios::beg);
            f.read(static_cast<char *>(outBuffer), size);
            return int32_t(f.gcount());
        }
    }
    if (!gAssets) return -1;
    AAsset *asset = AAssetManager_open(gAssets, path.c_str(), AASSET_MODE_RANDOM);
    if (!asset) return -1;
    int32_t got = -1;
    if (AAsset_seek64(asset, offset, SEEK_SET) == offset) got = AAsset_read(asset, outBuffer, size_t(size));
    AAsset_close(asset);
    return got;
}

bool exists(const std::string &path) {
    if (!safeRelative(path)) return false;
    if (!gInternal.empty()) {
        std::ifstream f(gInternal + "/" + path, std::ios::binary);
        if (f) return true;
    }
    if (!gAssets) return false;
    AAsset *asset = AAssetManager_open(gAssets, path.c_str(), AASSET_MODE_UNKNOWN);
    if (!asset) return false;
    AAsset_close(asset);
    return true;
}

} // namespace

realms::Engine &ensureEngine(android_app *app) {
    if (gEngine.initialized()) return gEngine;
    gAssets = app && app->activity ? app->activity->assetManager : nullptr;
    gInternal = app && app->activity && app->activity->internalDataPath ? app->activity->internalDataPath : "";

    realms::EngineConfig cfg;
    cfg.writableRoot = gInternal;
    cfg.readFile = [](const std::string &path, std::vector<unsigned char> &out) {
        if (!safeRelative(path)) return false;
        return readInternal(path, out) || readAsset(path, out);
    };
    cfg.readRange = [](const std::string &path, int64_t offset, int32_t size, void *out) -> int32_t {
        if (!safeRelative(path)) return -1;
        return readAssetRange(path, offset, size, out);
    };
    cfg.printSink = [](int level, const std::string &text) {
        const int prio = level == REALMS_PRINT_ERROR     ? ANDROID_LOG_ERROR
                         : level == REALMS_PRINT_WARNING ? ANDROID_LOG_WARN
                         : level == REALMS_PRINT_DEVELOPER ? ANDROID_LOG_DEBUG
                                                           : ANDROID_LOG_INFO;
        __android_log_print(prio, "Realms", "%s", text.c_str());
    };
    if (!gEngine.init(cfg)) {
        __android_log_print(ANDROID_LOG_ERROR, "Realms", "Realms engine failed to start");
        return gEngine;
    }
    realms::sandbox::BootOptions boot;
    boot.platform = "android";
    boot.fileExists = exists;
    realms::sandbox::boot(gEngine, boot);
    __android_log_print(ANDROID_LOG_INFO, "Realms", "Realms engine ready: core=%s origin=%s",
                        gEngine.backendName().c_str(), gEngine.backendOrigin().c_str());
    return gEngine;
}

void saveConfig() {
    realms::sandbox::saveUserConfig(gEngine);
}

} // namespace realms_android
