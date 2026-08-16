# rocksmith-psarc — build, run, test (requires CMake, zlib, libvorbis)

CMAKE       ?= cmake
BUILD_DIR   ?= build

# Default CDLC paths (relative to this Makefile = rocksmith-psarc/)
# Smoke/test harnesses take two paths; with one shipped song, reuse it for both.
PSARC1      ?= ../cdlc/psarc/Led-Zeppelin_Stairway-to-Heaven_v3_0RM_p.psarc
PSARC2      ?= $(PSARC1)
PSARC_OFFICIAL ?= tests/fixtures/SS_PonyIcon_p.psarc
OFFICIAL_CLASSIFY_PSARC ?= /Users/nicapotato/Documents/psarc/DLC/would_p.psarc

PSARC1_ABS  := $(abspath $(PSARC1))
PSARC2_ABS  := $(abspath $(PSARC2))

PSARC_OFFICIAL_ABS := $(abspath $(PSARC_OFFICIAL))

.PHONY: all configure build clean clean-vgmstream run test test-detail smoke official help wasm-build wasm-smoke wasm-clean

# Emscripten/WASM build (requires emcc + one-time: embuilder build zlib ogg vorbis)
WASM_BUILD_DIR ?= build-wasm
WASM_WEM ?= ../cdlc/wem/wish-you-were-here/song_wishyouwhizzhere.wem

# Matches CMake RS_VGM_BINARY_DIR in CMakeLists.txt (vgmstream is built here, not under $(BUILD_DIR)/)
VGM_CMAKE_BUILD ?= $(CURDIR)/.vgmstream-cmake-build

help:
	@echo "Targets:"
	@echo "  make / make build   Configure (if needed) and compile"
	@echo "  make configure      Run CMake configure only"
	@echo "  make clean          Remove $(BUILD_DIR)/ (does not remove vgmstream; see clean-vgmstream)"
	@echo "  make clean-vgmstream  Remove $(VGM_CMAKE_BUILD)/ (full vgmstream rebuild next build)"
	@echo "  make run            Build and run instrument listing + middle samples ($(PSARC1) $(PSARC2))"
	@echo "  make test           Same as run"
	@echo "  make test-detail    Same as run"
	@echo "  make smoke          Build and run pass/fail smoke test on both PSARCs"
	@echo "  make official       Build and run official PSARC fixture test (+ CDLC classify)"
	@echo ""
	@echo "Override PSARC paths: make run PSARC1=/path/a.psarc PSARC2=/path/b.psarc"

all: build

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR)

build: configure
	$(CMAKE) --build $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

clean-vgmstream:
	rm -rf $(VGM_CMAKE_BUILD)

run: build
	@echo "Using: $(PSARC1_ABS)"
	@echo "       $(PSARC2_ABS)"
	@$(BUILD_DIR)/rs_psarc_test "$(PSARC1_ABS)" "$(PSARC2_ABS)"

test: run

test-detail: run

smoke: build
	@$(BUILD_DIR)/rs_psarc_smoke "$(PSARC1_ABS)" "$(PSARC2_ABS)"

official: build
	@$(BUILD_DIR)/rs_psarc_official_test "$(PSARC_OFFICIAL_ABS)" "$(PSARC1_ABS)" "$(OFFICIAL_CLASSIFY_PSARC)"

wasm-build:
	@command -v emcc >/dev/null || { echo "emcc not found (install emscripten)"; exit 1; }
	@embuilder build zlib ogg vorbis
	emcmake $(CMAKE) -S . -B $(WASM_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(WASM_BUILD_DIR)

# Node smoke: PSARC parse (AES CFB/CTR + zlib) + WEM->WAV decode (vgmstream + vorbis).
wasm-smoke: wasm-build
	@echo "==== rs_psarc_smoke (Node) ===="
	-node $(WASM_BUILD_DIR)/rs_psarc_smoke.js "$(PSARC1_ABS)" "$(PSARC2_ABS)"
	@echo "==== rs_psarc_official_test (Node) ===="
	node $(WASM_BUILD_DIR)/rs_psarc_official_test.js "$(PSARC_OFFICIAL_ABS)" "$(PSARC1_ABS)" "$(OFFICIAL_CLASSIFY_PSARC)"
	@echo "==== rs_wem_smoke (Node) ===="
	node $(WASM_BUILD_DIR)/rs_wem_smoke.js "$(abspath $(WASM_WEM))"

wasm-clean:
	rm -rf $(WASM_BUILD_DIR) .vgmstream-cmake-build-wasm
