cmake_minimum_required(VERSION 3.22.1)
project(realms LANGUAGES C CXX)

# The real Android entry point: GameActivity -> native_app_glue -> GLES sandbox.
# No WebView, npm, JavaScript bundle or remote content is included in this APK.
# Realms Engine C++ bridge (repo-root realms/): Quake 2 cvars, command buffer,
# PAK filesystem. librealms_core.so (Free Pascal core) is picked up from
# jniLibs at runtime when present; otherwise the C++ reference core runs.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../realms ${CMAKE_CURRENT_BINARY_DIR}/realms-engine)

add_library(realms SHARED main.cpp AndroidOut.cpp Renderer.cpp RealmsAndroid.cpp TextureLoader.cpp ImageDecode.cpp GltfScene.cpp GltfRenderer.cpp MapSession.cpp AndroidMapMode.cpp)
target_compile_features(realms PRIVATE cxx_std_17)
find_package(game-activity REQUIRED CONFIG)
target_link_libraries(realms realms_engine game-activity::game-activity EGL GLESv3 android log)
