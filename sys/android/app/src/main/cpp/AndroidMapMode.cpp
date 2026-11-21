// Realms map mode on Android (see AndroidMapMode.h).
#include "AndroidMapMode.h"

#include "RealmsAndroid.h"

#include <android/log.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <vector>

AndroidMapMode::~AndroidMapMode() {
    // GL objects die with the context; only wait for a running worker.
    if (prepare_.valid()) prepare_.wait();
}

bool AndroidMapMode::open(realms::Engine &engine, const city::GltfRenderer &sandboxCity,
                          const std::string &sandboxCityPath, std::string &error) {
    close();
    realms::MapGeometry geometry;
    if (!session_.open(engine, geometry, error)) return false;

    const city::Scene &cityScene = sandboxCity.scene();
    if (session_.mesh() && session_.path() == sandboxCityPath && sandboxCity.ready() && cityScene.sourceMapped &&
        cityScene.sourceScale > 0.0f) {
        reuseCity_ = true;
        cityScale_ = cityScene.sourceScale;
        cityOffset_ = cityScene.sourceOffset;
        session_.summary() += ", textured";
        return true;
    }

    std::vector<sandbox::MeshVertex> vertices;
    mapview::toRenderVertices(geometry, session_.unitsPerMetre(), vertices);
    if (!vertices.empty()) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(sandbox::MeshVertex)), vertices.data(),
                     GL_STATIC_DRAW);
        using V = sandbox::MeshVertex;
        const GLsizei stride = sizeof(V);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, r)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, nx)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, u)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(V, layer)));
        glBindVertexArray(0);
        count_ = GLsizei(vertices.size());
    }

    if (session_.mesh()) {
        // The textured file in its own space; the worker reads storage and
        // assets directly (never the engine, which belongs to this thread).
        const std::string path = session_.path();
        prepare_ = std::async(std::launch::async, [path]() -> std::optional<city::Scene> {
            try {
                city::Scene scene;
                city::LoadOptions options;
                options.level = false;
                options.buildPhysics = false;
                options.keepSourceSpace = true;
                std::string log;
                if (scene.load(path, realms_android::readFile, log, options)) return scene;
                __android_log_print(ANDROID_LOG_WARN, "Realms", "map: %s: no textured model (%s)", path.c_str(),
                                    log.c_str());
            } catch (const std::exception &e) {
                __android_log_print(ANDROID_LOG_WARN, "Realms", "map: %s: %s", path.c_str(), e.what());
            }
            return std::nullopt;
        });
    }
    return true;
}

void AndroidMapMode::releaseTriangles() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vbo_ = vao_ = 0;
    count_ = 0;
}

void AndroidMapMode::close() {
    if (prepare_.valid()) prepare_.wait();
    prepare_ = {};
    model_.release();
    releaseTriangles();
    reuseCity_ = false;
    cityScale_ = 1.0f;
    cityOffset_ = {};
    session_.close();
}

void AndroidMapMode::advance() {
    if (prepare_.valid() && prepare_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::optional<city::Scene> scene;
        try {
            scene = prepare_.get();
        } catch (const std::exception &) {
        }
        prepare_ = {};
        if (scene) model_.beginUpload(std::move(*scene));
    }
    if (!model_.uploading()) return;
    std::string log;
    const auto start = std::chrono::steady_clock::now();
    while (model_.uploading() && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(4))
        model_.uploadStep(log);
    if (!log.empty()) __android_log_print(ANDROID_LOG_WARN, "Realms", "%s", log.c_str());
    if (model_.ready()) {
        releaseTriangles(); // the textured model replaces the plain triangles
        session_.summary() += ", textured";
    }
}

physics::Vec3 AndroidMapMode::drawEye() const {
    const physics::Vec3 e = session_.eye();
    return reuseCity_ ? mapview::toLevelScene(e, cityScale_, cityOffset_) : e;
}

void AndroidMapMode::draw(GLuint program, const city::GltfRenderer &sandboxCity) const {
    const physics::Vec3 eye = drawEye();
    if (reuseCity_) {
        sandboxCity.draw(program, eye.x, eye.z);
        return;
    }
    if (model_.ready()) {
        model_.draw(program, eye.x, eye.z);
        return;
    }
    if (!count_) return;
    glUniform1i(glGetUniformLocation(program, "modelPass"), 0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    // Map faces have a consistent winding, but brush-built maps and glTF
    // slabs can be seen from either side.
    glDisable(GL_CULL_FACE);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, count_);
    glBindVertexArray(0);
}
