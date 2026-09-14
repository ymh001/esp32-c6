# Candidate Projects for ESP32-C6 Touch AMOLED 2.16

Surveyed on 2026-09-14. Repository stars and update dates are snapshots, not
quality scores.

Only projects marked "exact" below explicitly target the Waveshare
ESP32-C6-Touch-AMOLED-2.16. Projects for other AMOLED sizes may be useful
references, but must not be treated as drop-in firmware.

## Recommended Order

| Rank | Project | Use case | Board support | Notes |
| --- | --- | --- | --- | --- |
| 1 | Waveshare official repository | Board bring-up and reference | Exact | Official examples, factory firmware, and a XiaoZhi snapshot |
| 2 | `mfellner/sparklet` | Clean ESP-IDF/LVGL application foundation | Exact | Pinned IDF 5.5.3, LVGL 9, tests, setup portal, public release |
| 3 | `78/xiaozhi-esp32` | AI voice assistant | Exact board directory | Very active upstream, but current versions require ESP-IDF 6.0.1+ |
| 4 | `HermannBjorgvin/Clawdmeter` | BLE desktop usage dashboard | Exact C6 environment | Popular and active, but tied to Claude usage and an unclear license |
| 5 | `vthinkxie/claude-desktop-buddy-esp32` | BLE desktop companion | Exact C6 board header | Arduino/PlatformIO, desktop integration, last active May 2026 |
| 6 | `kangjinshan/xiaozhi-kb` | Voice, BLE HID keyboard, SD recording, Wi-Fi microphone | Exact | MIT, complex, low stars, but detailed real-hardware validation |
| 7 | `noisefactorllc/noisedeck-nano` | Generative visuals and USB control | Exact | MIT, focused, good C6 memory strategy, recent |
| 8 | `Ropaxyz/AgileTracker` | Wi-Fi electricity price dashboard | Exact | Complete Arduino project, but UK Octopus Agile specific |
| 9 | `marciovicente/wisp-ai` | Claude Code desktop mascot | C6 BSP exists | Main README still documents ESP32-S3, so the C6 path needs review |

## Ready-to-Flash Candidates

### 1. Waveshare Official Repository

- Repository:
  https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16
- Framework: Arduino, ESP-IDF 5.5.x, and a complete XiaoZhi v2.2.5 tree
- Status: Official reference; last pushed 2026-08-13 in the surveyed snapshot
- Best for: Initial hardware validation, pin confirmation, and safe fallback
- Includes:
  - AXP2101, IMU, RTC, SD, Wi-Fi, and audio examples
  - LVGL 8 and LVGL 9 examples
  - Factory application source
  - Prebuilt factory and XiaoZhi firmware images
- Caveats:
  - The Arduino example headers contain an incorrect-looking CS/INT pair. The
    complete XiaoZhi board definition and actual constructor defaults use
    `LCD_CS=15`, `TP_INT=5`, and `TP_RST=11`.
  - The examples directory is named ESP-IDF-v5.5.3 while the product docs and
    defaults reference 5.5.2.

This should be the baseline before importing any community project.

### 2. Sparklet

- Repository: https://github.com/mfellner/sparklet
- License: No blanket license grant is stated for all upstream material
- Framework: ESP-IDF 5.5.3, LVGL 9
- Release: `v1.0.0`, with firmware bundle and checksums
- Best for: A well-documented, production-like ESP-IDF template for a
  Wi-Fi-connected 480 x 480 dashboard
- Strengths:
  - Exact board pin map, including corrected CS 15 and touch interrupt 5
  - Low-memory partial rendering suitable for the C6
  - Wi-Fi setup portal, NVS persistence, cached data, dimming, and rotation
  - Host tests and physical-device validation notes
- Caveat: The application data source is a user's `sparkDash` server for DGX
  Spark clusters. Reusing it for another service requires replacing the API
  layer, not just changing colors.

### 3. XiaoZhi

- Upstream:
  https://github.com/78/xiaozhi-esp32
- Vendor snapshot:
  https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16/tree/main/02_Example/XiaoZhi-v2.2.5
- License: MIT upstream
- Best for: Offline wake word, cloud speech recognition, LLM chat, TTS, MCP,
  display UI, audio, battery, and Wi-Fi provisioning
- Upstream status: Very active; `v2.5.0` published 2026-09-10
- Caveat: Current upstream requires ESP-IDF 6.0.1 or later and recommends 6.1.
  The local toolchain is 5.5.2. Use the Waveshare v2.2.5 snapshot first unless
  installing a second ESP-IDF version is intentional.

### 4. Clawdmeter

- Repository: https://github.com/HermannBjorgvin/Clawdmeter
- Framework: PlatformIO/Arduino
- Best for: A polished Bluetooth dashboard showing Claude Code usage
- Strengths:
  - Explicit `waveshare_amoled_216_c6` firmware environment
  - macOS, Linux, and Windows host daemons
  - BLE data service and BLE HID side buttons
  - Active development and a large user base
- Caveats:
  - Product-specific to Claude usage.
  - No clear repository-wide license was reported by the GitHub API.
  - It reads host credentials and installs a background daemon.

### 5. Claude Desktop Buddy

- Repository: https://github.com/vthinkxie/claude-desktop-buddy-esp32
- Framework: PlatformIO/Arduino
- Best for: BLE pairing with Claude Desktop or Claude Code hardware-buddy UI
- Strengths:
  - Includes `board_waveshare_esp32c6_touch_amoled_2_16.h`
  - Shared UI and clean board-specific hardware layer
  - Documents PWR, IO10, BOOT, touch, PMU, RTC, IMU, and audio behavior
  - Much smaller than the complete XiaoZhi voice stack
- Caveats:
  - Built around the Claude Hardware Buddy protocol.
  - License metadata is `NOASSERTION`; review the repository license before
    reusing code.
  - Less recently updated than Clawdmeter.

### 6. XiaoZhi KB

- Repository: https://github.com/kangjinshan/xiaozhi-kb
- License: MIT
- Framework: ESP-IDF, derived from XiaoZhi
- Best for: The broadest all-in-one experiment found for this exact board
- Features:
  - XiaoZhi voice assistant
  - SD card recording and sound storage
  - BLE HID keyboard plus relative touchpad
  - Real-time stereo microphone streamed over Wi-Fi
  - On-screen application selector
- Strengths: Detailed pin caveats, memory behavior, BLE transport notes, and
  described physical-device validation
- Caveats:
  - Very large scope and many interacting subsystems.
  - Low star count; source must be reviewed before trusting it with credentials
    or local data.
  - Uses SD and display resources that share GPIO pins, which makes regression
    testing important.

### 7. Noisedeck Nano

- Repository: https://github.com/noisefactorllc/noisedeck-nano
- License: MIT
- Framework: Arduino CLI/ESP32 core
- Best for: A self-contained graphical instrument with no cloud dependency
- Features:
  - Plasma, zone plates, flow noise, and static
  - Touch, IMU shake, button, menu, and USB serial control
  - A small health-check protocol that can turn the device into a status screen
- Strengths:
  - Exact board hardware support
  - Band rendering designed around the C6's lack of PSRAM
  - Host-side effect tests and measured frame rates/free heap
  - Clear flashing and factory-restore notes
- Caveat: It is an art/noise instrument rather than a general dashboard.

### 8. AgileTracker

- Repository: https://github.com/Ropaxyz/AgileTracker
- License: MIT for the project; third-party components have separate licenses
- Framework: Arduino, LVGL
- Best for: Always-on Wi-Fi information display
- Features:
  - Octopus Agile electricity prices
  - Current, today, and next-day views
  - Brightness controls and scheduled night dimming
  - Rotation and touch gestures
- Strengths: Exact board, recent, simple dependency model, useful UI reference
- Caveat: The data source is specific to UK Octopus Agile tariffs.

### 9. Wisp AI

- Repository: https://github.com/marciovicente/wisp-ai
- License: MIT
- Framework: ESP-IDF
- Best for: A Claude Code status mascot with a macOS menu-bar application
- C6 status: The repository contains `bsp_c6_amoled_216`, and a community port
  document covers this board, but the main README still documents the ESP32-S3
  version.
- Caveat: Treat the C6 support as a port to verify rather than a stable,
  fully documented target.

## Useful References, Not Ready Firmware

### Codex Sidekick

- Repository: https://github.com/Keung123/ESP32-Codex-Sidekick
- Exact board: Yes
- Status: Mostly project plan, architecture, protocol, prompts, and empty
  firmware/bridge placeholders
- Value: Useful design notes for a local Codex status bridge
- Do not treat it as a finished firmware.

### CodeIsland Desktop Buddy

- Repository:
  https://github.com/DeyunMa/codeisland-desktop-buddy-esp32-c6-touch-amoled-2.16
- Exact board: Yes
- Status: Experimental
- Known limits: Physical buttons are not fully mapped and long-duration
  stability is pending.
- Value: Reference for the CodeIsland BLE protocol and a low-resolution scaled
  canvas.

### Board Documentation and Port Notes

- Repository: https://github.com/shankarcabus/esp32-c6-touch-amoled
- Content: Portuguese notes covering hardware, macOS setup, compatible
  projects, Clawdmeter, and a Wisp AI port
- Value: Good cross-checking material
- Caveat: No license is declared and it is not standalone firmware.

### Official Mirror Plus Experiments

- Repository: https://github.com/oxygen0827/ESP32-C6-AMOLED-2.16
- Exact board: Yes
- Content: Mirrored Arduino/ESP-IDF material plus a "Clare C6" application
- Caveat: No license is declared and provenance is not as clear as the
  Waveshare repository. Prefer the official source when they overlap.

### EBaji

- Repository: https://github.com/zhouyb96/EBaji
- Exact board: Yes
- Content: A hard-to-describe Arduino experiment with a Bilibili demo
- Caveat: No repository license and a custom forked LVGL library. Review
  carefully before reuse.

## Near-Neighbor Projects

These projects target the 2.06 or 1.8 inch boards. They are not directly
compatible, but can be useful references for watch UI, Rust, BLE, or board
structure:

- `jphein/esp32c6-watch`: Rust `no_std` watch firmware for the 2.06 inch board
- `UniqueDroid/esp-watchos`: Custom watch OS for the 2.06 inch board
- `astrixgame/astrafw`: ESP-IDF smartwatch firmware for the 2.06 inch board
- `mcuw/esp32-c6-amoled-2.06-sdk`: SDK for the 2.06 inch board
- `chayuto/ws-ESP32-C6-Touch-AMOLED-1.8`: Multiple C6 application examples for
  the 1.8 inch board

Porting these requires panel dimensions, offsets, orientation, reset rails,
touch controller, and memory strategy to be revalidated against this 2.16 inch
board.

## Suggested First Targets

| Goal | First choice |
| --- | --- |
| Verify the board and preserve a recovery path | Waveshare factory firmware and `09_LVGL_V9_Test` |
| Build a clean custom ESP-IDF application | Export board support from Sparklet |
| Run an AI voice assistant | Waveshare XiaoZhi v2.2.5 before upgrading to upstream IDF 6 |
| Build a desk status dashboard | Clawdmeter or Claude Desktop Buddy |
| Explore graphics and low-memory rendering | Noisedeck Nano |
| Build a Wi-Fi information display | AgileTracker or Sparklet |

Before importing any project, record its exact commit, license, required
framework version, and whether it shares the display/SD bus safely.
