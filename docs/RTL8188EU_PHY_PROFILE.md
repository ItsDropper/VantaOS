# RTL8188EU PHY profile format

The RTL8188EU driver now has a bounded profile loader/executor for board- and silicon-cut-specific initialization. The repository does **not** contain a verified RTL8188EU profile. Without a profile, the driver remains fail-closed and does not enable radio transmission.

The profile is a system-integrator input supplied at build time as `RTL8188EU_PHY_PROFILE=/path/to/profile.bin`. `build.sh` installs it as `/SYSTEM/RTL8188EU.PHY`. It is separate from `RTL8188EU_FIRMWARE`, which supplies `/SYSTEM/RTL8188EU.BIN`.

## Binary format (little-endian)

The 24-byte header is:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 8 | ASCII `V8EUPHY1` |
| 8 | 2 | Format version, currently 1 |
| 10 | 2 | Header size, 24 |
| 12 | 4 | Total file size, including header |
| 16 | 4 | CRC-32/IEEE of all record bytes |
| 20 | 1 | Exact RTL8188E chip cut; wildcard `0xff` is rejected |
| 21 | 1 | Reserved, must be zero |
| 22 | 2 | Record count |

Each record is 16 bytes:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 1 | Operation code |
| 1 | 1 | Initialization phase |
| 2 | 1 | Reserved, zero |
| 3 | 1 | Reserved, zero |
| 4 | 2 | Register address |
| 6 | 2 | Reserved, zero |
| 8 | 4 | Register mask |
| 12 | 4 | Value / expected value |

The record array must exactly fill the declared file size. The loader checks the magic, version, lengths, exact chip cut, reserved bytes, CRC, operation/address ranges, masks, and phase ordering before it starts writing registers. Wildcard-cut profiles are rejected because RF and calibration values must not be applied to an unidentified silicon cut. Writes are read back and checked. Polls are bounded to 512 reads; delay operations require the kernel tick source and are capped at 100 ticks.

## Operations

| Code | Operation | Address space |
| ---: | --- | --- |
| 1 | Write 8-bit MAC register | address below `0x800` |
| 2 | Write 16-bit MAC register | address below `0x800` |
| 3 | Write 32-bit MAC register | address below `0x800` |
| 4 | Write 32-bit baseband register | `0x800–0x0fff` |
| 5 | Write 20-bit RF path-A register | RF address `0x00–0xff` |
| 6–10 | Poll corresponding MAC/BB/RF register | Same range as matching write |
| 11 | Delay by kernel ticks | address and mask must be zero; value 1–100 |

For writes, a zero mask means the full register width; otherwise the driver performs a masked read-modify-write and verifies the selected bits. Polls require a nonzero mask and succeed only when all masked bits match the expected value.

## eFuse-derived transmit-power data

The driver decodes the three 2.4-GHz channel-group CCK and HT40 base indices
from the RTL8188E logical eFuse map, along with the packed signed OFDM and
HT20 deltas. The groups correspond to channels 1–3, 4–9, and 10–14. Missing
(`0xff`) or out-of-range base indices are rejected instead of being replaced
with guessed values. A separate helper selects the channel group and derives
bounded CCK, OFDM, HT20, and HT40 indices with signed-delta handling and 6-bit
clamping. These are still software calibration inputs: the helper does not
program transmit-power registers or prove the board's RF output is correct.

## Required phases

Records must be grouped in nondecreasing phase order:

1. MAC register setup
2. Baseband setup
3. AGC setup
4. eFuse-derived, board-specific TX-power programming
5. IQ calibration sequence and completion/result polls

Phases 1–4 must each contain at least one write. Phase 5 must contain at least one write and one poll. After phases 1–4 pass, the driver applies the factory crystal trim, requests LC calibration with bounded completion polling, restores the prior RF channel/bandwidth state, and only then executes phase 5. The software TX gate opens only after the entire profile and calibration sequence succeeds.

## Safety and validation limits

- CRC-32 detects accidental corruption; it is **not** a signature or authenticity check. Only use a profile obtained from a trusted source and validated for the exact adapter board and silicon cut.
- This format is an execution mechanism, not a source of calibration values. Do not fill it with guessed register values or a profile for a different board.
- A syntactically valid profile can still be electrically or functionally wrong. Host CI cannot establish safe RF power, correct calibration, actual scanning, association, WPA2 traffic, or Internet connectivity.
- No profile is currently bundled with VantaOS. Until a correct profile is provided and physical-adapter tests pass, end-to-end RTL8188EU support is **not complete**.
