# MLRws — local Makefile for the dual-mono build.
#
# Usage:
#   make firmware      Build firmware (MLRws.uf2)
#   make all           Build dual-mono firmware
#   make flash         Build + flash firmware
#   make clean         Remove build directory

SHELL := /bin/bash

PICO_SDK_PATH ?= /opt/pico-sdk
CMAKE_ARGS := -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico -DPICO_SDK_PATH=$(PICO_SDK_PATH)

BUILD := build
OPENOCD_HOST     ?= host.docker.internal
OPENOCD_GDB_PORT ?= 3333
GDB ?= $(shell command -v arm-none-eabi-gdb 2>/dev/null || command -v gdb-multiarch 2>/dev/null)

.PHONY: all firmware flash clean help

all: firmware

firmware:
	@mkdir -p $(BUILD)
	@cd $(BUILD) && cmake .. $(CMAKE_ARGS) -DCARD_NAME=MLRws
	@cd $(BUILD) && ninja -j4

flash: firmware
	@echo "Flashing MLRws..."
	@$(GDB) -q -nx -batch \
		-ex "target extended-remote $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)" \
		-ex "monitor reset init" \
		-ex "load" \
		-ex "monitor reset run" \
		-ex "detach" \
		$(BUILD)/MLRws.elf
	@echo "FLASH OK"

clean:
	@rm -rf $(BUILD)

help:
	@echo "MLRws build targets:"
	@echo "  make firmware      Build firmware (MLRws.uf2)"
	@echo "  make all           Build firmware"
	@echo "  make flash         Build + flash firmware"
	@echo "  make clean         Remove build directory"
