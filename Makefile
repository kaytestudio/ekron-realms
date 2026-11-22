# -----------------------------------------------------------------------------
# Ekron Realms - convenience wrapper around the CMake build (see BUILD.md).
#
#   make              configure + build everything into $(BUILD_DIR):
#                     ekron-realms (C++ SDL2 sandbox), librealms_core (Free
#                     Pascal core), librealms_game (Quake 2 game module),
#                     libekron_sandbox + realms-pascal (Free Pascal client
#                     running the C++ sandbox), realms-launcher
#   make test         run the CTest suite
#   make run          start the game (C++ executable)
#   make run-pascal   start the game through the Free Pascal client
#   make pascal       build only realms-pascal (and the sandbox library it needs)
#   make game         build only librealms_game, the Quake 2 game module (game/)
#                     map mode plays BSP maps with (realms::GameServer)
#   make lazarus      build realms-pascal from Projects/realms.lpi with lazbuild
#   make package      .deb (Linux) / .dmg + .tgz (macOS) with CPack
#   make install      install (PREFIX=/usr/local by default)
#   make clean        remove build outputs; make distclean removes $(BUILD_DIR)
#
# Variables: BUILD_DIR=build  BUILD_TYPE=Release  GENERATOR="Ninja" (when
# ninja is installed)  CMAKE_FLAGS=...  LAZBUILD=lazbuild  PREFIX=/usr/local
# -----------------------------------------------------------------------------

BUILD_DIR   ?= build
BUILD_TYPE  ?= Release
CMAKE       ?= cmake
CTEST       ?= ctest
CPACK       ?= cpack
LAZBUILD    ?= lazbuild
PREFIX      ?= /usr/local
CMAKE_FLAGS ?=
GENERATOR   ?= $(if $(shell command -v ninja 2>/dev/null),Ninja,)
JOBS        ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
GAME_BIN   := $(BUILD_DIR)/EkronRealms.app/Contents/MacOS/EkronRealms
PASCAL_BIN := $(BUILD_DIR)/EkronRealms.app/Contents/MacOS/realms-pascal
else
GAME_BIN   := $(BUILD_DIR)/ekron-realms
PASCAL_BIN := $(BUILD_DIR)/realms-pascal
endif

.PHONY: all build configure test run run-pascal pascal game lazarus package install clean distclean help

all: build

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR) $(if $(GENERATOR),-G "$(GENERATOR)",) \
	    -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(PREFIX) \
	    -DENABLE_FPC_CLIENT=ON -DENABLE_PASCAL_CORE=ON $(CMAKE_FLAGS)

configure: $(BUILD_DIR)/CMakeCache.txt

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

run: build
	$(GAME_BIN)

run-pascal: pascal
	$(PASCAL_BIN)

pascal: configure
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS) --target realms_pascal_client

game: configure
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS) --target realms_game_module

# Projects/realms.lpi builds libekron_sandbox first (Projects/build_sandbox.sh)
# and writes build/realms-pascal, the same program as `make pascal`. The
# Lazarus project always uses <repo>/build, whatever BUILD_DIR says.
lazarus:
	$(LAZBUILD) Projects/realms.lpi

package: build
	cd $(BUILD_DIR) && $(CPACK)

install: build
	$(CMAKE) --install $(BUILD_DIR)

clean:
	-$(CMAKE) --build $(BUILD_DIR) --target clean

distclean:
	rm -rf $(BUILD_DIR)

help:
	@sed -n '2,21p' Makefile
