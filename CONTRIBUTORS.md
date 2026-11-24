# Contributors and credits

How to contribute: [CONTRIBUTING.md](CONTRIBUTING.md).

## Contributors

Ekron Realms is developed by Kayte Studio. People with commits in this repository (see the [contributors graph](https://github.com/kaytestudio/ekron-realms/graphs/contributors) for the current list):

- [@vic3t3chn0](https://github.com/vic3t3chn0)
- [@nbnbnjkii](https://github.com/nbnbnjkii)

## Code the project builds on

- **Quake 2** by id Software (GPL): the engine layer (`realms/`), the C++ ports in `realms/src/` (`Q2Pmove.cpp`, `GameServer.cpp`, `GameEffects.cpp`, ...) and the game module (`game/`) follow its source.
- **Quake2 Delphi conversion** by Juha Hartikainen and the Quake2Delphi project ([readme.txt](readme.txt), [installation.txt](installation.txt)): the Pascal units under `game/` come from it, and the Pascal Realms core (`realms/pascal/`) is derived from its `qcommon` units.
- Vendored libraries in `sys/android/app/src/main/cpp/third_party/`, under their own licenses:
  - [nlohmann/json](https://github.com/nlohmann/json) 3.10.4 (MIT)
  - [tinygltf](https://github.com/syoyo/tinygltf) 2.9 (MIT)
  - [stb_image](https://github.com/nothings/stb) 2.32 and stb_vorbis 1.25 (public domain or MIT)
- [SDL2](https://www.libsdl.org/) (zlib license) for the desktop client, and the Pascal SDL2/SDL3 bindings in the `tools/SDL2-for-Pascal` and `tools/SDL3-for-Pascal` submodules.

## Assets

- Sounds and music: CC0, sources listed in [sounds/LICENSE.md](sys/android/app/src/main/assets/sounds/LICENSE.md).
- 3D models: authors, sources and license notes in [models/README.md](sys/android/app/src/main/assets/models/README.md) (in Russian). Some models are under their own Sketchfab terms and are **not** covered by the project's license.

## License

The project's own code is under the GNU Affero General Public License v3.0, see [LICENSE](LICENSE). The code and assets listed above keep their own licenses.
