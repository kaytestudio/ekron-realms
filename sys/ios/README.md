# ЭКРОН — нативная iOS-песочница (`sys/ios`)

Проект **`sys/ios/realms/realms.xcodeproj`** (схема **`realms iOS`**) реализует нативную iOS-версию городской песочницы **ЭКРОН** на **C++17 + Metal + UIKit**.

## Архитектура

- **Общее ядро мира и физики (`sys/android/app/src/main/cpp/`)**: без дублирования кода напрямую подключены `SandboxWorld.h`, `ObjectDynamics.h`, `PlayerPhysics.h`, `ToolInteraction.h`, `FpvDrone.h`, `RaycastPlacement.h`, `Interaction.h`, `WorldRecovery.h`, `LevelPlacement.h`, `GltfScene.h/.cpp` и `ImageDecode.cpp`.
- **Общие ассеты (`sys/android/app/src/main/assets/`)**: модели города (`abandoned_city_street_for_games.glb`), УАЗ-469 (`uaz_469_1973_classic.glb`), пульта и FPV-дрона, текстуры материалов, иконки каталога, `config/spawn.json`, `intro.mp4`, а также пример карты Quake 2 `maps/courtyard.glb` и её скайбокс `env/` упаковываются в `realms.app` по ссылкам в `realms.xcodeproj` без дублирования бинарных файлов в Git.
- **Metal-рендерер (`SandboxBridge.h/.mm`, `realms Shared/Shaders.metal`)**:
  - 8-слойный `MTLTextureType2DArray` (`sRGB` + mipmaps) для процедурных и строительных материалов;
  - glTF-рендеринг города, УАЗ-469, 3D-пульта и FPV-дрона с сортировкой прозрачных батчей и голографическим предпросмотром строительства;
  - отдельный offscreen-проход `480×270` для живого экрана видеопотока с камеры FPV-дрона над пультом;
  - автосохранение построек в `Application Support/EkronSandbox/city-glb-v1.txt`.
- **Русский сенсорный интерфейс (`GameViewController.swift`, `TouchControlsView.swift`, `BuildCatalogView.swift`)**:
  - мультитач: левый джойстик движения, правая область обзора, прыжок и приседание;
  - радиальное колесо инструментов (`Грави-пушка`, `Пульт FPV`, `Пустые руки`), каталог строительства и мебели, удаление, заморозка и вращение объектов;
  - двухстиковое управление FPV-дроном (Mode 2) со сменой вида (FPV / орбитальная камера);
  - меню паузы (режим полёта noclip, возврат на дорогу, восстановление УАЗ-469, возврат на спавн) и копирование координат `spawn.json` в буфер обмена.

## Сборка unsigned IPA (локально на macOS и в GitHub Actions)

Workflow **`.github/workflows/ios-sandbox.yml`** запускает все 29 модульных C++-тестов на `clang++`, проверяет компиляцию Metal-шейдеров (`tools/validate_ios_shaders.py`) и собирает неподписанный `ekron-native-sandbox-debug.ipa`:

```sh
xcodebuild archive \
  -project "sys/ios/realms/realms.xcodeproj" \
  -scheme "realms iOS" \
  -configuration Debug \
  -destination "generic/platform=iOS" \
  -sdk iphoneos \
  -archivePath "/tmp/ekron-ios-sandbox.xcarchive" \
  DEVELOPMENT_TEAM="" \
  CODE_SIGN_STYLE=Manual \
  CODE_SIGN_IDENTITY="" \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGNING_ALLOWED=NO \
  AD_HOC_CODE_SIGNING_ALLOWED=YES
```
