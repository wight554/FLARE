# Phase 8: Settings TLV / Delta Schema Migration — Specification

**Goal**: Implement non-destructive schema evolution for RP2040 NOR flash persistence. Replace rigid flat struct with a compact Tag-Length-Value (TLV) stream, enabling seamless firmware upgrades without wiping operator calibration while maintaining ping-pong atomicity and v63 backward compatibility.
**Scope**: Firmware (`settings_store.c`, `settings_store.h`), host test harnesses (`tests/host/test_persistence.c`), static parity guard (`scripts/test_settings_parity.py`), and documentation (`CONTEXT.md`, `MANUAL.md`).

---

## 1. Flash Layout & Wire Format

### 1.1 Sector Allocation & Buffer Headroom
- Dual ping-pong sectors:
  - Sector A: `0x1FE000` (`PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE`)
  - Sector B: `0x1FF000` (`PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE`)
- Sector erase size: `FLASH_SECTOR_SIZE` = 4096 bytes (4 KB).
- Buffer size: `SETTINGS_FLASH_BUFFER_BYTES` = 1024 bytes (4 flash pages, expanded from 512B).
- Current payload requirement: ~75 entries consuming ~420 bytes, leaving >550 bytes headroom for future tags.

### 1.2 Binary Wire Format (Packed TLV)
- **Header** (16 bytes):
  ```c
  typedef struct {
      uint32_t magic;       // SETTINGS_MAGIC = 0x4e4f5346u ("FSON")
      uint32_t version;     // SETTINGS_VERSION = 64u
      uint32_t seq;         // Monotonic sequence number
      uint16_t payload_len; // Length of serialized TLV stream in bytes
      uint16_t reserved;    // 0
  } settings_header_t;
  ```
- **TLV Record Stream**:
  - Sequence of packed records: `[uint8_t tag, uint8_t len, uint8_t val[len]]`.
  - Tags: `settings_tag_t` enum (`1` .. `255`).
  - Length: `len` specifies exact data payload size in bytes (e.g. 4 for `int32_t`/`float`, 1 for `bool`, 8 for `int[2]`).
- **Trailer** (4 bytes):
  - `uint32_t crc32` stored immediately after payload at offset `sizeof(settings_header_t) + payload_len`.
  - Computed over `[0, sizeof(settings_header_t) + payload_len)` with standard polynomial `0xEDB88320u`.

---

## 2. Backward Compatibility & Lazy Migration (v63 -> v64)

### 2.1 Legacy v63 Flat Struct Definition
- Retain frozen `settings_t_v63` layout in `settings_store.c`:
  - 136 bytes flat struct used in Phase 7 (`version == 63`).
  - Validation: verifies `s->magic == SETTINGS_MAGIC`, `s->version == 63`, and CRC over `[0, offsetof(settings_t_v63, crc32))`.

### 2.2 Lazy Migration Strategy
- On boot (`settings_load`):
  1. Inspect Sector A and Sector B.
  2. Validate each sector against v64 (TLV) or v63 (legacy flat).
  3. Select newest valid sector using signed sequence arithmetic: `(int32_t)(seq_a - seq_b) > 0`.
  4. If chosen sector is v63:
     - Unpack legacy fields into runtime globals.
     - Keep `g_seq` and `g_active_sector` pointing to legacy sector.
     - Do NOT perform eager flash write on boot (ensures firmware rollback safety).
  5. On subsequent `settings_save()`:
     - Serialize current runtime globals into new v64 TLV format.
     - Write to alternate sector.
     - Once verified, active sector flips to new v64 sector.

---

## 3. Forward Compatibility & Unknown Tags

### 3.1 Unknown Tag Handling
- During deserialization (`settings_load`):
  - If `tag` is unrecognized: parser advances `offset += 2 + len` without error.
  - Zero RAM spillover buffer: unknown tags are not retained in RAM.
- During serialization (`settings_save`):
  - Saver emits only tags known to the running firmware compiled schema.
  - On downgrade + save: unknown newer tags are pruned cleanly.

### 3.2 Strict Parser Bounds Checking
- Parser invariants:
  - `payload_len + sizeof(settings_header_t) + sizeof(uint32_t) <= SETTINGS_FLASH_BUFFER_BYTES` (1024).
  - Loop condition: `offset + 2 <= payload_len`.
  - Record bounds check: `offset + 2 + len <= payload_len`. If false, payload is malformed -> abort parsing, fallback to compiled defaults.
  - Schema length check: For known tag `T`, if `len != expected_len(T)`, record is skipped without crashing or out-of-bounds read.

---

## 4. Test Harness & Parity Verification

### 4.1 Static Parity Script (`scripts/test_settings_parity.py`)
- Parse `settings_tag_t` enum definitions.
- Verify every tag in `settings_tag_t` is:
  1. Emitted in `settings_save()`.
  2. Handled in `settings_load()`.
  3. Default-initialized in `settings_defaults()`.
- Check tag uniqueness and schema length consistency.

### 4.2 Host Simulation Tests (`tests/host/test_persistence.c`)
- Test fresh board (0x00 and 0xFF).
- Test ping-pong alternation and brownout recovery with 1024B buffer.
- Test legacy v63 flat flash load & lazy migration to v64 TLV on save.
- Test unknown tag skipping in TLV stream.
- Test truncated/malformed TLV record bounds safety.
