## MODIFIED Requirements

### Requirement: Full resync recovery

The daemon SHALL emit a full `SET_MMU` (all fields) on the first push and on a
board-online transition. On the periodic resync tick the daemon SHALL read the Klipper
`mmu` object and emit a full `SET_MMU` only when the reported mock state diverges from the
desired field set; when they match the daemon SHALL emit nothing that tick. A restarted
Klipper/Moonraker SHALL recover complete state within one tick of the divergence becoming
observable.

#### Scenario: Klipper restarts mid-print

- **WHEN** the resync tick reads the `mmu` object and finds it diverged (or absent) from
  the desired field set, or the board comes online
- **THEN** the next push contains the full field set

#### Scenario: Idle resync tick with mock in sync

- **WHEN** the resync tick reads the `mmu` object and every mirrored field already matches
  the desired value
- **THEN** no `SET_MMU` is emitted that tick
