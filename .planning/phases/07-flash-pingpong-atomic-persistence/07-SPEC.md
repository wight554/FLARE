# Phase 7: Flash Ping-Pong Atomic Persistence — Specification

**Goal**: Deliver power-loss resilient atomic settings persistence for RP2040 NOR flash using dual ping-pong sectors (A/B) with monotonic sequence numbering and CRC32 integrity checks.
**Scope**: Firmware (`settings_store.c`, `settings_store.h`), host test harnesses (`tests/host/test_persistence.c`, `tests/host/CMakeLists.txt`, `sim_main.c`), static parity guard (`scripts/test_settings_parity.py`), test runner (`scripts/test_persistence.py`), and documentation.

---

## 1. Flash Layout & Header Architecture

### 1.1 Dual Sector Allocation (8 KB Total)
- Sector A: `PICO_FLASH_SIZE_BYTES - (2 * FLASH_SECTOR_SIZE)` (Offset: `0x1FE000` on 2 MB flash).
- Sector B: `PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE` (Offset: `0x1FF000` on 2 MB flash).
- Sector size: `FLASH_SECTOR_SIZE` = 4096 bytes.
- Buffer write size: `SETTINGS_FLASH_BUFFER_BYTES` = 512 bytes (covers `settings_t` packed struct).

### 1.2 `settings_t` Header & Versioning
- Header structure:
  ```c
  typedef struct {
      uint32_t magic;    // SETTINGS_MAGIC = 0x4e4f5346u ("FSON")
      uint32_t version;  // SETTINGS_VERSION = 63u (bumped from 62u)
      uint32_t seq;      // Monotonic sequence number (starts at 1)
      ...
      uint32_t flash_erase_count;
      uint32_t crc32;    // CRC32 over [0, offsetof(settings_t, crc32))
  } settings_t;
  ```
- `SETTINGS_VERSION` bumped from `62u` to `63u`.
- Parity guard: `seq` added to `SKIP_FIELDS` in `scripts/test_settings_parity.py`.

---

## 2. Load & Fallback Logic (`settings_load`)

### 2.1 Validity Criteria
A sector is VALID if and only if:
1. `magic == SETTINGS_MAGIC` (0x4e4f5346u).
2. `version == SETTINGS_VERSION` (63u).
3. Recomputed CRC32 over all bytes preceding `crc32` equals stored `crc32`.

### 2.2 Sector Arbitration
1. Inspect Sector A and Sector B:
   - Compute `valid_a` and `valid_b`.
2. Selection rules:
   - **Both Valid**: Choose newer sector using signed sequence arithmetic:
     `((int32_t)(seq_a - seq_b) > 0) ? Sector A : Sector B`.
     Handles modular 32-bit sequence wraparound cleanly.
   - **Only One Valid**: Choose the valid sector.
   - **Neither Valid**: Fall back to `settings_defaults()`, initialize runtime `g_active_sector = 1`, `g_seq = 0`, apply default TMC parameters, do NOT write back to flash.
3. Active State Initialization:
   - On successful load: set `g_active_sector` to selected sector index (0 for A, 1 for B), and `g_seq` to selected sector `seq`.
   - Apply motion, TMC, cutter, sync, and reload parameters into runtime globals.

---

## 3. Atomic Save Sequence (`settings_save`)

### 3.1 Ping-Pong Step Sequence
1. Identify target sector: `target = 1 - g_active_sector` (toggles between Sector A [0] and Sector B [1]).
2. Advance sequence and wear metrics:
   - `g_seq++`.
   - `g_flash_erase_count++`.
   - If `g_flash_erase_count == FLASH_WEAR_WARN_THRESHOLD`: emit `EV:FLASH:WEAR_WARNING`.
3. Populate candidate `settings_t` struct:
   - Set `s.magic = SETTINGS_MAGIC`.
   - Set `s.version = SETTINGS_VERSION`.
   - Set `s.seq = g_seq`.
   - Set `s.flash_erase_count = g_flash_erase_count`.
   - Populate all runtime tunables.
   - Compute `s.crc32 = crc32_buf((const uint8_t *)&s, offsetof(settings_t, crc32))`.
   - Zero-pad into 512-byte flash buffer.
4. Stop all motion: call `stop_all()`.
5. Critical section:
   - `uint32_t ints = save_and_disable_interrupts();`
   - `flash_range_erase(target_offset, FLASH_SECTOR_SIZE);`
   - `flash_range_program(target_offset, buffer, SETTINGS_FLASH_BUFFER_BYTES);`
   - `restore_interrupts(ints);`
6. Post-Write Readback Verification:
   - Read programmed struct directly via XIP memory mapping at `(const settings_t *)(XIP_BASE + target_offset)`.
   - Validate magic, version, CRC32, and matching `seq`.
   - **Only on successful validation**: commit active pointer `g_active_sector = target`.
   - **On validation failure**: retain `g_active_sector` pointing to prior valid sector (retains brownout recovery guarantee).

---

## 4. Host Simulation & Brownout Verification

### 4.1 Host Test Executable (`tests/host/test_persistence.c`)
- Add standalone test executable compiled against RAM-backed flash shim.
- Verification scenarios:
  1. Fresh board all 0x00 / all 0xFF: falls back to defaults, does not write back.
  2. Ping-pong alternation: successive saves toggle 0 -> 1 -> 0 -> 1 with incrementing `seq` and `flash_erase_count`.
  3. Single valid sector load: works when either A or B is valid and the other is empty or garbage.
  4. Sequence precedence: arbitration picks newer sector when both are valid.
  5. Sequence wraparound: handles 32-bit counter overflow correctly.
  6. Brownout mid-program simulation: corrupting CRC/payload of target sector during save leaves prior sector valid; next `settings_load()` cleanly recovers prior settings.
  7. Brownout mid-erase simulation: target sector wiped to 0xFF; next `settings_load()` cleanly recovers prior settings.
  8. In-flight write verification failure leaves active sector pointer unchanged.

### 4.2 Static Parity & Regression Gate
- `scripts/test_settings_parity.py`: Passes with `seq` in `SKIP_FIELDS`.
- `scripts/test_persistence.py`: Executes host persistence test as part of `python3 -m unittest discover`.
- `validate_regression.py`: Full clean pass across all 10 gate stages.
