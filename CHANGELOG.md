# Changelog

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
