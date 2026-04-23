# MLRws — local Makefile for mono and stereo builds.
#
# Usage:
#   make mono          Build mono variant (MLRws_mono.uf2)
#   make stereo        Build stereo variant (MLRws_stereo.uf2)
#   make all           Build both
#   make flash-mono    Build + flash mono
#   make flash-stereo  Build + flash stereo
#   make clean         Remove both build directories

SHELL := /bin/bash

PICO_SDK_PATH ?= /opt/pico-sdk
CMAKE_ARGS := -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico -DPICO_SDK_PATH=$(PICO_SDK_PATH)

BUILD_MONO   := build
BUILD_STEREO := build-stereo

OPENOCD_HOST     ?= host.docker.internal
OPENOCD_GDB_PORT ?= 3333
GDB ?= $(shell command -v arm-none-eabi-gdb 2>/dev/null || command -v gdb-multiarch 2>/dev/null)

.PHONY: all mono stereo flash-mono flash-stereo clean help

all: mono stereo

mono:
	@mkdir -p $(BUILD_MONO)
	@cd $(BUILD_MONO) && cmake .. $(CMAKE_ARGS) -DCARD_NAME=MLRws_mono -DCARD_STEREO=OFF
	@cd $(BUILD_MONO) && ninja -j4

stereo:
	@mkdir -p $(BUILD_STEREO)
	@cd $(BUILD_STEREO) && cmake .. $(CMAKE_ARGS) -DCARD_NAME=MLRws_stereo -DCARD_STEREO=ON
	@cd $(BUILD_STEREO) && ninja -j4

flash-mono: mono
	@echo "Flashing MLRws_mono..."
	@$(GDB) -q -nx -batch \
		-ex "target extended-remote $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)" \
		-ex "monitor reset init" \
		-ex "load" \
		-ex "monitor reset run" \
		-ex "detach" \
		$(BUILD_MONO)/MLRws_mono.elf
	@echo "FLASH OK (mono)"

flash-stereo: stereo
	@echo "Flashing MLRws_stereo..."
	@$(GDB) -q -nx -batch \
		-ex "target extended-remote $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)" \
		-ex "monitor reset init" \
		-ex "load" \
		-ex "monitor reset run" \
		-ex "detach" \
		$(BUILD_STEREO)/MLRws_stereo.elf
	@echo "FLASH OK (stereo)"

clean:
	@rm -rf $(BUILD_MONO) $(BUILD_STEREO)

help:
	@echo "MLRws build targets:"
	@echo "  make mono          Build mono (MLRws_mono.uf2)"
	@echo "  make stereo        Build stereo (MLRws_stereo.uf2)"
	@echo "  make all           Build both"
	@echo "  make flash-mono    Build + flash mono"
	@echo "  make flash-stereo  Build + flash stereo"
	@echo "  make clean         Remove build directories"
