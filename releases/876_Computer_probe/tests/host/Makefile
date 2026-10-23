# Host tests for the Computer-Probe core. `make` builds and runs them.
# With the usb_host_check test card built, they also round-trip its real UF2.

SRC := ../../src
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -I$(SRC) -fsanitize=address,undefined

REAL_UF2 := ../usb_host_check/build/usb_host_check.uf2
REAL_BIN := ../usb_host_check/build/usb_host_check.bin

.PHONY: all test clean
all: test

test_core: test_core.cpp $(SRC)/payload.cpp $(SRC)/flasher.cpp $(SRC)/ghostfat.cpp $(wildcard $(SRC)/*.h)
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp $(SRC)/payload.cpp $(SRC)/flasher.cpp $(SRC)/ghostfat.cpp

test: test_core
	FAT_IMAGE=ghostfat.img ./test_core $(if $(wildcard $(REAL_UF2)),$(REAL_UF2) $(REAL_BIN))
	@if command -v fsck.fat >/dev/null; then fsck.fat -n ghostfat.img; else echo "(fsck.fat not installed: skipped volume check)"; fi

clean:
	rm -f test_core ghostfat.img
