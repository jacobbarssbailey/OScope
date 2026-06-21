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
