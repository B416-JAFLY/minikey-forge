# Changelog

## 2026-10-23 — ST7735S LCD hardware SPI driver & Apple Pay / Face ID UI animations

- Root-caused the previous blank LCD issue via schematic and factory V1.6 firmware analysis: `SPI_CS_LCD` on MSU2 MINI is wired to `PC3` (not only `PA3`), and the 0.96" ST7735S panel requires explicit power/VCOM/gamma register initialization (`0xB1..0xC5`, `0xE0..0xE1`) plus optional factory calibration read from SPI Flash (`0xFFF8..0xFFFC`).
- Switched `src/board.c` to hardware `SPI1` (12 MHz) with safe `PA6` multiplexing between Flash `MISO` (`IPU`) and LCD `D/C` (`Out_PP`), and partial dirty-region updates (`hero`, `card`, `bar`) for smooth ~35 FPS animation.
- Added `src/ui.h` procedural 4× sub-pixel anti-aliased RGB565 UI engine implementing:
  - USB connection / boot intro (`0..900ms` `ONLINE` cyan-blue ring sweep, Face ID padlock shackle snap-shut with flash and shockwave halo, transitioning to `READY` breathing ring & orbiting comet).
  - Touch confirmation prompt (`TOUCH` dual orbiting comets and sonar ripples) with live capacitive touch press feedback (`HOLD` contracting emerald ripples during debounce) and signing state (`VERIFY`).
  - Apple Pay / Face ID confirmation success animation (`PASSED` emerald badge pop-in, two-stroke elastic checkmark draw, 4 celebration sparkles and diagonal specular sheen sweep).
  - Damped horizontal shake and progressive coral `×` draw on `DENIED` / `FAULT`.
- Added 10 host tests in `tests/test_ui_animation.py` (67 total tests passing) and verified live display + touch confirmation + FIDO2 Reset / MakeCredential / GetAssertion flows on the physical CH32X033F8P6 MINI board.

## 2026-10-06 — Reset-only maintenance tool

- Add `reset-device.py` for an explicitly requested wipe of all credentials and PIN, without flashing firmware, rotating the device master key or creating replacement test credentials.
- Arm before normal replug, require a single MINI with the expected application AAGUID, and verify PIN unset, eight retries, rejection of known old IDs and empty resident test RPs after CTAP Reset.
- Archive active browser public credential records only after successful reset verification, allowing the local pages to start fresh. Keep the acceptance log and archived records outside Git.
- Five utility tests and two existing firmware reset tests passed. Device reset acceptance is recorded locally after the user runs the tool and confirms presence.
- Hardware reset acceptance passed: PIN unset, retry budget restored to eight, four known old credential IDs rejected and three test RPs empty. No replacement credential was created; firmware and master key were retained. Both local browser test pages returned to an unregistered state.

## 2026-10-06 — PIN / resident credentials / GC candidate

- Defer LCD repairs at user's request; archive diagnosis and remove LCD initialization, rendering and animation from firmware.
- Implement CTAP2.0 ClientPIN protocol 1, PIN-based UV, PIN change and persistent retry budget.
- Add resident user records, discoverable login, multiple account assertions and replacement of a resident credential for the same RP/user.
- Replace 240-entry lifetime log with two-bank authenticated compaction in the same 64 KiB reservation; support 48 live credentials and continued signatures.
- Add guarded v1 storage migration preserving keys and counters; never erase an overlapping legacy migration source.
- Move local build master key to ignored `.env`; refuse implicit generation or key rotation. Add Git privacy rules.
- 56 host tests pass, including write/erase interruptions and old-format migration; hardware and native-browser acceptance of this candidate remain pending. Earlier firmware completed native browser login and persistence, but display remained black.

- Candidate flashed and read-back verified on CH32X033F8P6; normal FIDO HID enumeration restored. PIN/resident/browser migration acceptance still pending. LCD remains intentionally disabled.
- Hardware acceptance passed: user-defined PIN setup, UP + UV flags, resident account discovery without allowList, RP binding and independent ES256 verification. Original browser credential also logged in after migration with its counter increasing. Native browser resident-passkey and power-cycle checks remain pending.
- Native Windows browser resident registration and PIN/UV-required discovery login passed server verification. Add a real local-Git regression test for first-time pinned dependency checkout (57 tests total).
- Final hardware acceptance: native Windows resident passkey registration, PIN/UV login and normal power-cycle login all succeeded. Existing non-resident credential remained valid through migration. Browser resident signature counter reached 6. Preserve remote README rewrite while integrating checkout fix and final acceptance.
