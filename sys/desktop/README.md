# ЭКРОН — Нативный клиент для ПК (`sys/desktop`)

Нативный десктопный клиент городской песочницы **ЭКРОН** для **macOS (`.app`)**, **Linux (`.deb`)**, **Windows 10 / 11 (`.zip`, x64 и ARM64)** и портативных консолей на aarch64 Linux вроде **Anbernic RG40XXH** (порт PortMaster), использующий общее C++17-ядро мира/физики (`sys/android/app/src/main/cpp`) и общие 3D-ассеты (`sys/android/app/src/main/assets`) на базе **SDL2 + OpenGL 3.3 Core** (или OpenGL ES 3.0), а также опциональный лаунчер на **Free Pascal (`realms-launcher`)**.

## Управление (клавиатура и мышь)

- **`W A S D`** — передвижение (при пилотировании FPV-дрона: `W/S` — тяга, `A/D` — рыскание)
- **Мышь** — обзор (при пилотировании FPV-дрона: крен и тангаж, также стрелки)
- **`Пробел` / `C` / `Z`** — прыжок (подъём в полёте) / приседание / лечь
- **`1` / `2` / `3`** — `Грави-пушка` / `Пульт FPV` / `Пустые руки`
- **`Q` / `Tab` / `B`** — каталог строительства и техники (`Материалы`, `Строительство`, `Мебель`, `Техника`)
- **`G`** — магазин: скины войнух и золотые монеты для улучений (работает и в онлайн-режиме)
- **`ЛКМ`** — захватить/отпустить объект, установить объект, подключить FPV-дрон
- **`ПКМ` / `F`** — заморозить объект или отменить строительство
- **`R` / колесо мыши** — повернуть объект на `90°`
- **`X` / `Delete`** — удалить установленный объект
- **`E`** — взаимодействие (двери, УАЗ-469, диван, FPV-дрон)
- **`V` / `H`** — переключение камеры FPV-дрона / возврат дрона домой
- **`N` / `P` / `F11` / `Esc`** — режим полёта (noclip) / копировать `spawn.json` / полный экран / меню паузы
- **`L`** — сменить язык интерфейса (English → Русский → Português → Español)
- **`F1`** (на картах Quake 2) — компьютер помощи: сложность, название уровня, задачи карты, убитые монстры, цели и секреты; повторное `F1` закрывает его
- **`Tab` / `[` `]` / `Enter`** (на картах Quake 2) — инвентарь: открыть/закрыть, выбрать предыдущий/следующий предмет, использовать выбранный
- **`F6` / `F9`** (на картах Quake 2) — быстрое сохранение / быстрая загрузка (`save quick` / `load quick`; в консоли — `save <имя>` и `load <имя>`). Сохранения лежат в папке пользовательских данных, в `save/<имя>/`
- На картах Quake 2 над нижней панелью — строка состояния игры (здоровье, патроны, броня, подобранный предмет, таймеры); с `pak0.pak` — с иконками и цифрами Quake 2. `cl_statusbar 0` скрывает её, тогда здоровье, броня и патроны выводятся текстом в верхней панели

## Геймпад

Любой контроллер, который знает SDL (Xbox, PlayStation, Switch Pro, встроенный геймпад портативной консоли), управляет всей игрой; клавиатура и мышь работают параллельно. Кнопки названы по раскладке Xbox.

- **Левый стик** — передвижение (в городе также крестовина); **правый стик** — обзор
- **`Start`** — меню паузы (`Esc`); в меню крестовина вверх/вниз выбирает кнопку, **`A`** нажимает, **`B`** закрывает. Каталог и магазин требуют мыши, **`B`** закрывает их
- **В городе:** `A` — прыжок (подъём в полёте), `B` — приседание (спуск в полёте), `X` — взаимодействие (`E`), `Y` — поворот (`R`), `RT` / `LT` — левый / правый клик, `LB` / `RB` — предыдущий / следующий инструмент
- **На картах Quake 2:** `A` или `LT` — прыжок, `B` — приседание, `RT` — огонь, `LB` / `RB` — предыдущее / следующее оружие, `X` — использовать предмет, `Y` — инвентарь, крестовина: вверх — компьютер помощи, вниз — счёт, влево / вправо — предыдущий / следующий предмет; `Back` + `LB` / `RB` — быстрое сохранение / загрузка

Полная таблица — в [docs/CONTROLS.md](../../docs/CONTROLS.md#gamepad).

## Заставка

При запуске, как на Android и iOS, проигрывается видео-заставка `intro.mp4` (из каталога ассетов) со звуком,
с чёрными полями по краям, если пропорции окна отличаются от 16:9. Пока она идёт, модели мира загружаются
в фоновом потоке; если заставку пропустить раньше, до конца загрузки показывается экран «Загрузка мира…».

- **`Esc` / `Пробел` / `Enter` / щелчок мыши** — пропустить заставку;
- флаг **`--no-intro`** или переменная окружения **`EKRON_NO_INTRO=1`** — запуск без заставки.

Видео декодирует `IntroVideo.h` с одной из реализаций, выбираемой при сборке:
**AVFoundation** на macOS (`IntroVideoApple.mm`, ничего ставить не нужно), **Media Foundation** в Windows
(`IntroVideoMediaFoundation.cpp`, входит в систему; выпускам Windows N нужен Media Feature Pack, без него игра
запускается без заставки) и **FFmpeg** на Linux
(`IntroVideoFFmpeg.cpp`, библиотеки `libavformat` / `libavcodec` / `libswscale` / `libswresample` версии 5.1+).
Звук реализаций FFmpeg и Media Foundation подмешивается к звуку игры в `DesktopAudio`. Если FFmpeg не найден, клиент собирается
без заставки (`IntroVideoNone.cpp`) и пишет об этом при конфигурации CMake; `-DEKRON_REQUIRE_INTRO_VIDEO=ON`
(так собирают CI и Docker) превращает это в ошибку, а `-DEKRON_INTRO_VIDEO=OFF` отключает заставку совсем.
Тест `tests/native/intro_video_test.cpp` проигрывает заставку через FFmpeg или Media Foundation с поддельными часами и аудиовыходом.

## Язык интерфейса

Клиент полностью переведён на **английский** (`en`), **русский** (`ru`), **европейский португальский** (`pt-PT`)
и **испанский (Испания)** (`es-ES`).
Язык выбирается в таком порядке:

1. флаг `--lang <код>` (`en`, `ru`, `pt-PT` или `es-ES`);
2. переменная окружения `EKRON_LANG`;
3. выбор, сохранённый в меню паузы (кнопка «Язык» или клавиша `L`) — файл `settings.json` в каталоге настроек пользователя;
4. язык системы (`SDL_GetPreferredLocales`, затем `LANGUAGE` / `LC_ALL` / `LC_MESSAGES` / `LANG`);
5. английский по умолчанию.

Все строки находятся в `LocalizationStrings.h` (по одной строке на ключ, все четыре языка рядом),
API — в `Localization.h`. Тест `tests/native/localization_test.cpp` проверяет полноту переводов,
плейсхолдеры `{0}` и наличие всех символов в растровом шрифте HUD (`DesktopFont.h`).

## Данные Quake 2 (`baseq2`)

Для режима карт Quake 2 (`+map base1`, звуки и текстуры `.wal`) скопируйте папку `baseq2` из установленной Quake 2
вместе с `pak0.pak` в каталог данных пользователя (`~/.local/share/KayteStudio/EkronRealms/baseq2/` в Linux,
`~/Library/Application Support/KayteStudio/EkronRealms/baseq2/` в macOS, `%APPDATA%\KayteStudio\EkronRealms\baseq2\`
в Windows) или в каталог ассетов (`<ассеты>/baseq2/`).
Можно оставить её на месте и указать флаг **`--quake2 <каталог>`** или переменную окружения **`EKRON_QUAKE2_DIR`**;
подходит и папка Quake 2, и сама `baseq2`.

- Подключаются `pak0.pak` … `pak9.pak` из корня каждого каталога и из его `baseq2/`; более поздний pak важнее, pak важнее отдельных файлов.
- Отдельные файлы сначала ищутся как `<каталог>/<путь>`, потом как `<каталог>/baseq2/<путь>`.
- `--engine-info` выводит строку `Quake 2:`, а при запуске в журнал пишется `[Ekron] Quake 2 data:`: найденные pak-файлы или куда их скопировать.
- Флаги вроде `--quake2` указывайте до `+map`: флаг после `+команды` завершает эту команду.

Модели — формат Quake 3 `.md3` (IDP3, версия 15), а не `.md2` из Quake 2. Игра по-прежнему называет модели
`models/<путь>/tris.md2`, а режим карт загружает вместо них `models/<путь>/tris.md3` (отдельным файлом или из pak).
Файлы `.md2` не читаются, поэтому только с `pak0.pak` монстры, предметы и оружие рисуются коробками,
пока не добавлены `.md3` с теми же именами (например, в `baseq2/pak1.pak` или отдельными файлами).

- Номера кадров те же, что в игре: кадр N в `.md3` — кадр N игры (`stand01`, `run1`, …).
- Каждая поверхность рисуется своей текстурой — именем шейдера поверхности (`.tga` или 8-битный `.pcx`);
  если файла нет, пробуется другой из `.tga` / `.pcx` с тем же именем; имя без расширения — как `.tga`, затем `.pcx`.
- Номер скина сущности (например, скин боли) выбирает шейдер поверхности с этим индексом, иначе первый.
- Текстурные координаты повторяются за пределами 0..1, как в Quake 3; теги читаются, но не используются.

## Сборка

В GitHub Actions сборка разделена на четыре независимых workflow:
- **`.github/workflows/desktop-debian.yml`** — сборка и проверка пакета Debian (`.deb`, артефакт `ekron-realms-linux-deb`);
- **`.github/workflows/desktop-macos.yml`** — сборка и проверка бандла macOS (`EkronRealms.app`, артефакт `ekron-realms-macos-app`);
- **`.github/workflows/desktop-windows.yml`** — сборка Windows (MinGW-w64 и Visual C++ на Windows Server 2025, серверной версии
  Windows 11 24H2), тесты и ZIP-архив (`ekron-realms-<версия>-windows-x64.zip`, артефакт `ekron-realms-windows-zip`), а также
  нативная сборка для Windows 11 на ARM (LLVM-MinGW на `windows-11-arm`: `ekron-realms-<версия>-windows-arm64.zip`,
  артефакт `ekron-realms-windows-arm64-zip`);
- **`.github/workflows/handheld-portmaster.yml`** — сборка OpenGL ES для портативных консолей на aarch64 (Anbernic RG40XXH),
  проверка шейдеров как GLSL ES 3.00 и порт PortMaster (`ekron-realms-<версия>-portmaster-aarch64.zip`).

### Linux (Debian/Ubuntu `.deb`)
```sh
# Для заставки: sudo apt-get install libavformat-dev libavcodec-dev libswscale-dev libswresample-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j
(cd build && cpack -G DEB)
```

### macOS (`EkronRealms.app`)
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# Готовый бандл: build/EkronRealms.app
```

### Windows 10 / 11 (`.zip`)
В оболочке MSYS2 **UCRT64** (`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`)
с распакованным пакетом `SDL2-devel-2.x.y-mingw.tar.gz` (например, в `C:/SDL2`):
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL2_DIR=C:/SDL2/cmake \
  -DFPC_EXECUTABLE=C:/lazarus/fpc/3.2.2/bin/x86_64-win64/fpc.exe   # необязательно: realms_core.dll и realms_game.dll
cmake --build build
(cd build && cpack)   # build/ekron-realms-<версия>-windows-x64.zip
```
Visual Studio 2022 тоже подходит (`-G "Visual Studio 17 2022" -A x64` и пакет `SDL2-devel-2.x.y-VC.zip`), см. `BUILD.md`.
Архив — переносимая папка: `ekron-realms.exe`, `SDL2.dll`, библиотеки Free Pascal и `assets\`; установщика нет.
Сохранения и настройки лежат в `%APPDATA%\KayteStudio\EkronRealms\`. Exe не подписан, поэтому SmartScreen может
предупредить при первом запуске: «Подробнее» → «Выполнить в любом случае». В Windows нет лаунчеров на Free Pascal
(`realms-launcher`, `realms-pascal`) и экспериментального онлайн-присутствия.

Для **Windows 11 на ARM** — то же в оболочке MSYS2 **CLANGARM64**, но SDL2 собирается из исходников (у пакета MinGW нет
сборки ARM64), а ядро и игровой модуль на Free Pascal собирает кросс-компилятор aarch64-win64 (`ppcrossa64.exe`) из форка
основной ветки Free Pascal (ни один релиз пока не поддерживает aarch64-win64, см. `BUILD.md`). Архив называется
`ekron-realms-<версия>-windows-arm64.zip`.

### Портативные консоли (Anbernic RG40XXH, PortMaster)
На aarch64 Linux (лучше Ubuntu 22.04, чтобы glibc была не новее, чем в прошивках):
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEKRON_GLES=ON -DEKRON_INTRO_VIDEO=OFF -DENABLE_FPC_CLIENT=OFF
cmake --build build
tools/make_portmaster_zip.sh build ekron-realms-portmaster-aarch64.zip
```
`-DEKRON_GLES=ON` делает OpenGL ES 3.0 первым выбором; переменная окружения `EKRON_GLES=1` / `EKRON_GLES=0` переопределяет
его в любой сборке. Порт использует SDL2 и драйвер GLES прошивки. **Ekron Realms** запускает город, **Ekron Realms - Quake 2** —
карту `base1` с `pak0.pak` в `ports/ekronrealms/baseq2/`; настройки и сохранения — в `ports/ekronrealms/conf/`.
