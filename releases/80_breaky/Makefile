export PICO_SDK_PATH := $(CURDIR)/pico-sdk

PROJECT := breaky
BUILD_DIR := build
PORT ?= /dev/ttyACM0
PICO_SDK_VERSION := 2.2.0
PICO_SDK_STAMP := pico-sdk/.breaky-sdk-$(PICO_SDK_VERSION)
NPROCS := $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

.PHONY: build upload clean pico-sdk encode

build: $(PICO_SDK_STAMP) src/breaky_audio.h $(BUILD_DIR)/Makefile
	cmake --build $(BUILD_DIR) --parallel $(NPROCS)
	test -f $(BUILD_DIR)/$(PROJECT).uf2

upload: build
	./scripts/upload_uf2.sh "$(BUILD_DIR)/$(PROJECT).uf2" "$(PORT)"

encode: src/breaky_audio.h

src/breaky_audio.h: data/amen_bpm170_freak.wav scripts/encode_audio.py
	python3 scripts/encode_audio.py data/amen_bpm170_freak.wav src/breaky_audio.h

$(BUILD_DIR)/Makefile: CMakeLists.txt pico_sdk_import.cmake src/main.cpp src/ComputerCard.h src/breaky_audio.h $(PICO_SDK_STAMP)
	cmake -S . -B $(BUILD_DIR)

pico-sdk: $(PICO_SDK_STAMP)

$(PICO_SDK_STAMP):
	@if [ ! -d pico-sdk/.git ]; then \
		git clone https://github.com/raspberrypi/pico-sdk pico-sdk; \
	fi
	cd pico-sdk && git fetch --tags --depth 1 origin $(PICO_SDK_VERSION)
	cd pico-sdk && git checkout $(PICO_SDK_VERSION)
	cd pico-sdk && git submodule update --init --recursive
	touch $(PICO_SDK_STAMP)

clean:
	rm -rf $(BUILD_DIR)
	rm -f src/breaky_audio.h
