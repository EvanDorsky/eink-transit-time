.PHONY: build upload monitor

build:
	pio run

upload:
	pio run -t upload

monitor:
	pio device monitor

symbols:
	pio run -t compiledb
