# demucs-mlx raylib app (macOS Apple Silicon)
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO := $(abspath $(ROOT)/..)
BUILD := $(ROOT)/build
BIN := $(BUILD)/demucs_mlx_app
PYTHON := $(REPO)/.venv/bin/python

.PHONY: all configure build run clean bundle worker models verify-mp3 verify-psarc externals version

all: build

externals:
	bash "$(ROOT)/scripts/setup_externals.sh"

configure: externals
	@mkdir -p "$(BUILD)"
	cd "$(BUILD)" && cmake .. -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build "$(BUILD)" -j
	@echo "Built $(BIN)"

version: build
	"$(BIN)" --version

run: build
	DEMUCS_MLX_PYTHON="$(PYTHON)" DEMUCS_MLX_CACHE="$(ROOT)/models" "$(BIN)"

# Headless-ish: process files then quit when queue drains
# Usage: make process FILES="a.mp3 b.psarc" OUT=~/Documents/psarc/stems
process: build
	@test -n "$(FILES)" || (echo "Usage: make process FILES='song.mp3' OUT=~/Documents/psarc/stems"; exit 1)
	DEMUCS_MLX_PYTHON="$(PYTHON)" DEMUCS_MLX_CACHE="$(ROOT)/models" \
		"$(BIN)" $(FILES) --out "$(or $(OUT),$(HOME)/Documents/psarc/stems)" --mp3

verify-mp3: build
	@mkdir -p "$(ROOT)/build/verify_out"
	DEMUCS_MLX_PYTHON="$(PYTHON)" DEMUCS_MLX_CACHE="$(ROOT)/models" \
		"$(BIN)" "$(ROOT)/testdata/short_20s.mp3" --out "$(ROOT)/build/verify_out" --mp3
	@ls -la "$(ROOT)/build/verify_out/short_20s/"
	@test -f "$(ROOT)/build/verify_out/short_20s/guitar.mp3"
	@test -f "$(ROOT)/build/verify_out/short_20s/vocals.mp3"
	@echo "verify-mp3 OK"

verify-psarc: build
	@mkdir -p "$(ROOT)/build/verify_out"
	DEMUCS_MLX_PYTHON="$(PYTHON)" DEMUCS_MLX_CACHE="$(ROOT)/models" \
		"$(BIN)" "$(HOME)/Documents/psarc/CDLC/Parquet-Courts_Freebird-II_v1_p.psarc" \
		--out "$(ROOT)/build/verify_out" --mp3
	@ls -la "$(ROOT)/build/verify_out/Parquet-Courts_Freebird-II_v1_p/"
	@test -f "$(ROOT)/build/verify_out/Parquet-Courts_Freebird-II_v1_p/guitar.mp3"
	@echo "verify-psarc OK"

models:
	@mkdir -p "$(ROOT)/models"
	DEMUCS_MLX_CACHE="$(ROOT)/models" "$(PYTHON)" -c "from demucs_mlx.model_converter import get_mlx_model; get_mlx_model('htdemucs_6s'); print('ok')"

worker:
	bash "$(ROOT)/scripts/freeze_worker.sh"

bundle: build worker
	bash "$(ROOT)/scripts/build_app.sh"

clean:
	rm -rf "$(BUILD)" "$(ROOT)/dist"
