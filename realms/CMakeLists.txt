# Realms Engine - C++ side of the bridge.
#
# Consumed by the desktop build (root CMakeLists.txt) and the Android NDK build
# (sys/android/app/src/main/cpp/CMakeLists.txt). The Free Pascal core
# (realms/pascal) is built separately by the desktop build when fpc is found,
# or with realms/pascal/build_core.sh for Android/iOS cross targets.
cmake_minimum_required(VERSION 3.16...3.31)

if(TARGET realms_engine)
    return()
endif()

add_library(realms_engine STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/Engine.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/NativeCore.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/NativeCModel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/SandboxBoot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/GameSave.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/MeshMap.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/MapGeometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/MapWalker.cpp"
)
target_include_directories(realms_engine PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(realms_engine PUBLIC cxx_std_17)
set_target_properties(realms_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(UNIX AND NOT APPLE)
    # dlopen() for librealms_core.so and librealms_game.so (desktop Linux and Android).
    target_link_libraries(realms_engine PUBLIC ${CMAKE_DL_LIBS})
endif()

# Link a prebuilt static Pascal core instead of loading it at runtime
# (used for iOS-style static deployments; see realms/README.md).
set(REALMS_PASCAL_STATIC_LIBRARY "" CACHE FILEPATH "Prebuilt static librealms_core.a to link (optional)")
if(REALMS_PASCAL_STATIC_LIBRARY)
    target_compile_definitions(realms_engine PUBLIC REALMS_PASCAL_STATIC=1)
    target_link_libraries(realms_engine PUBLIC "${REALMS_PASCAL_STATIC_LIBRARY}")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # -ffp-contract=off: collision maths stays bit-exact with the Pascal core.
    target_compile_options(realms_engine PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

# The Quake 2 stack as one module for the sandboxes (desktop, Android, iOS):
# the game server that hosts librealms_game, entity interpolation and .md3
# models, client effects, game sounds, BSP lightmaps, and realms::Quake2Module
# tying them together (realms/include/realms/Quake2Module.h). Renderer and
# audio API free; each sandbox draws / plays what the module hands out.
add_library(realms_quake2 STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/Q2Pmove.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/GameServer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/GameClient.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/GameView.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/GameEffects.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/GameSound.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/GameLayout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/GameHudCanvas.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/MapLighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/MapSky.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/MapTextures.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/Md3Model.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/Image.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/Quake2Module.cpp"
)
target_link_libraries(realms_quake2 PUBLIC realms_engine)
target_compile_features(realms_quake2 PUBLIC cxx_std_17)
set_target_properties(realms_quake2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(realms_quake2 PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
