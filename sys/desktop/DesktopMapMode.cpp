// Realms map mode for the desktop client (see DesktopMapMode.h).
#include "DesktopMapMode.h"

#include <vector>

namespace ekron::desktop {

bool DesktopMapMode::open(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer, std::string &error) {
    close(gl, renderer);
    realms::MapGeometry geometry;
    if (!session_.open(engine, geometry, error)) return false;

    // glTF maps: draw the textured file itself, in its own space.
    if (session_.mesh()) {
        auto model = std::make_unique<DesktopGltfModel>();
        const realms::Engine &reader = engine;
        city::LoadOptions options;
        options.level = false;
        options.buildPhysics = false;
        options.keepSourceSpace = true;
        std::string log;
        if (model->loadWithReader(engine.mapPath(),
                                  [&reader](const std::string &p, std::vector<unsigned char> &out) {
                                      return reader.loadFile(p, out);
                                  },
                                  log, options)) {
            model->upload(gl);
            if (model->ready()) model_ = std::move(model);
        }
        if (!model_) engine.print("map: " + engine.mapPath() + ": no textured model, drawing collision triangles\n");
    }

    std::vector<sandbox::MeshVertex> vertices;
    if (!model_) mapview::toRenderVertices(geometry, session_.unitsPerMetre(), vertices);
    renderer.setMapGeometry(gl, vertices);
    if (model_) session_.summary() += ", textured";
    return true;
}

void DesktopMapMode::close(ekron::gl::Api &gl, DesktopRenderer &renderer) {
    if (model_) {
        model_->release(gl);
        model_.reset();
    }
    if (session_.active()) renderer.clearMapGeometry(gl);
    session_.close();
}

} // namespace ekron::desktop
