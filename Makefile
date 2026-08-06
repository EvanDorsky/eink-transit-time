.PHONY: build upload monitor fonts

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
