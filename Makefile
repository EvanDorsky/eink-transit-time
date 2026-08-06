.PHONY: build upload monitor fonts preview

GFX_DIR = .pio/libdeps/crowpanel_epaper_579/Adafruit GFX Library
NORTH ?= 3,8,15
SOUTH ?= 5,12
BUS ?= 7,22

build:
	pio run

upload:
	pio run -t upload

monitor:
	pio device monitor

symbols:
	pio run -t compiledb

fonts:
	./scripts/gen_fonts.sh

preview:
	@mkdir -p host/build
	c++ -O2 -std=c++17 -DARDUINO=100 -Ihost/shim -I"$(GFX_DIR)" -Iinclude \
		host/preview.cpp src/render.cpp "$(GFX_DIR)/Adafruit_GFX.cpp" \
		-o host/build/preview
	./host/build/preview "$(NORTH)" "$(SOUTH)" "$(BUS)" host/build/preview.pgm
	python3 scripts/pgm2png.py host/build/preview.pgm preview.png
