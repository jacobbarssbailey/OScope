# OScope dev shortcuts — wraps PlatformIO.
# Run `just` (or `just --list`) to see available recipes.

# Show available recipes
default:
    @just --list

# Compile the firmware
build:
    pio run

# Build and upload to the Teensy
run:
    pio run -t upload

# Upload, then open the serial monitor
debug:
    pio run -t upload && pio device monitor

# Run the host-side AcqCore unit tests
test:
    pio test -e native

# Render UI previews to tools/preview/out (pass a screen name to render one)
preview *SCREENS:
    python3 tools/preview/screens.py {{SCREENS}}
