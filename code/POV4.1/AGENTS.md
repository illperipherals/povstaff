# AGENTS.md

This guide is for agentic coding assistants working in this repository.
It captures how to build/test and the local C++/Arduino style.

## Project overview
- PlatformIO project targeting `adafruit_qtpy_esp32s3_n4r2` (ESP32-S3).
- Primary code lives in `src/` as Arduino `.ino` and C++ `.cpp/.h`.
- The build env is defined in `platformio.ini`.

## Build, lint, and test
All commands assume the repo root.

### Build
- Build firmware: `pio run -e adafruit_qtpy_esp32s3_n4r2`
- Clean build: `pio run -e adafruit_qtpy_esp32s3_n4r2 -t clean`
- Upload to board: `pio run -e adafruit_qtpy_esp32s3_n4r2 -t upload`
- Serial monitor: `pio device monitor --baud 115200`

### Lint / static analysis
- Run PlatformIO static checks: `pio check -e adafruit_qtpy_esp32s3_n4r2`
- Use `pio check --skip-packages` if 3rd-party libs are noisy.
- There is no dedicated clang-format config in this repo.

### Tests
- Run all tests: `pio test -e adafruit_qtpy_esp32s3_n4r2`
- Run a single test (name or pattern):
  - `pio test -e adafruit_qtpy_esp32s3_n4r2 -f test_name`
  - `pio test -e adafruit_qtpy_esp32s3_n4r2 --filter test_*pattern*`
- Tests live under `test/` (see `test/README`).

## Repo layout
- `src/` contains the application: `POV4_1.ino`, `webserver.ino`, `LSM6.cpp`, `LSM6.h`.
- `include/` is for shared headers (see `include/README`).
- `lib/` is for private libraries (see `lib/README`).
- `.pio/` is build output and should not be edited.

## Code style guidelines
Follow the existing style in the file you touch. This codebase has mixed
indentation; match local conventions instead of reformatting unrelated code.

### Formatting
- Indentation:
  - `src/POV4_1.ino` uses 4 spaces in most blocks.
  - `src/webserver.ino`, `src/LSM6.cpp`, `src/LSM6.h` generally use 2 spaces.
  - Match the file you are editing.
- Braces: K&R style (`if (...) {` on same line).
- One statement per line; avoid trailing whitespace.
- Keep line lengths reasonable for Arduino IDE readability (aim < 100).
- Use blank lines to separate logical sections (includes, constants, globals).

### Imports / includes
Use the order seen in `src/POV4_1.ino`:
1. Arduino/framework headers (e.g., `<Arduino.h>`, `<Wire.h>`).
2. Third-party libraries (e.g., `<Adafruit_NeoPixel.h>`, `<WebServer.h>`).
3. Project headers (e.g., "LSM6.h").

### Types and constants
- Prefer fixed-width types for I/O or protocol data (`uint8_t`, `int16_t`).
- Use `const` for global constants where possible.
- Use `#define` for hardware pins and compile-time configuration, as in
  `POV4_1.ino` and `LSM6.cpp`.
- Keep floating-point math explicit (`0.000001` instead of implicit casts).

### Naming
- Classes and types: `PascalCase` (e.g., `LSM6`).
- Functions: `lowerCamelCase` (e.g., `setupWebserver`, `setNewImageChange`).
- Global variables: `lowerCamelCase` (e.g., `lastIMUcheck`).
- Constants/macros: `UPPER_SNAKE_CASE` (e.g., `NUM_PIXELS`, `A_SENS`).
- Keep names short but descriptive; prefer clarity over brevity in new code.

### Error handling and robustness
- Check return values from I/O and filesystem calls.
- On critical failures, follow existing patterns:
  - Print a `Serial` message for debugging.
  - Use LED or staff blink patterns for visual feedback.
  - In setup-critical failure, block with `while (1)` and indicate error.
- Avoid silent failures; log with `Serial.println` where practical.

### Memory and performance
- Avoid dynamic allocation in tight loops; prefer static or stack objects.
- When allocating (e.g., `new WebServer`), ensure only one instance and
  ownership is clear.
- Use `F("...")` for constant strings sent to `Serial` or `WebServer`.

### Arduino/PlatformIO specifics
- `setup()` should initialize hardware, filesystems, and peripherals.
- `loop()` should stay lightweight and avoid long blocking delays.
- Prefer `millis()` for timing instead of `delay()` in active loops.
- If `delay()` is needed, keep it short and explain why.

### Filesystem and webserver behavior
- LittleFS is expected; format-on-mount is currently enabled in
  `src/POV4_1.ino` via `LittleFS.begin(true)`.
- The webserver in `src/webserver.ino` has no access control; avoid exposing
  sensitive files or credentials in the filesystem.

## Safety and secrets
- Do not commit real WiFi credentials or secrets.
- `src/POV4_1.ino` contains placeholder SSID/password; keep placeholders when
  editing unless explicitly asked to set real values.

## No additional agent rules
- No Cursor rules found in `.cursor/rules/` or `.cursorrules`.
- No Copilot rules found in `.github/copilot-instructions.md`.

## When you change code
- Keep changes minimal and aligned with local style.
- Do not reformat unrelated code or generated files.
- If you add tests, document how to run them.
