# Minimal build configuration for inference runner
# Disables camera, LCD, and other unused features to speed up builds
# Usage: Include this in your cmake command with -C build_config_minimal.cmake

# Disable GLCD/LCD UI features
set(GLCD_UI OFF CACHE BOOL "Disable GLCD UI to save RAM and build time" FORCE)

# Disable camera support
set(ALIF_CAMERA_ENABLED OFF CACHE BOOL "Disable camera support" FORCE)

# Keep UART enabled (needed for your use case)
# CONSOLE_UART is already set to 2 by default

# Keep GPIO support (needed for your GPIO test routines)
# GPIO support is built-in to the platform drivers

# Keep SE Services if needed for power management
# set(SE_SERVICES_SUPPORT OFF CACHE BOOL "Disable SE Services" FORCE)

message(STATUS "=== Minimal Build Configuration ===")
message(STATUS "GLCD_UI: OFF")
message(STATUS "ALIF_CAMERA_ENABLED: OFF")
message(STATUS "Only UART, GPIO, and TFLite enabled")
message(STATUS "===================================")
