## ADDED Requirements

### Requirement: TUNING.md documents the hardened relay confidence gate

The TUNING.md relay section SHALL document the hardened
confidence-gate defaults, the `relay_min_flip_mm` anti-chatter knob,
the new deep-COMPRESSION collapse-ramp config keys, and the bimodal
deep-TENSION failure mode that motivated them, so an operator
understands why a flip-heavy bimodal print runs on the fallback driver
and how to tune the residual cycle. The guidance MUST stay consistent
with the existing self-contained, jargon-free, copy-paste-verified
style of the guide.

#### Scenario: Operator reads the relay gate guidance

- **WHEN** an operator opens the TUNING.md relay section
- **THEN** it states that flip-heavy bimodal prints intentionally stay
  unconfident (fallback drives), lists the hardened
  `relay_confidence_cycles` / `relay_confidence_window_ms` defaults, the
  `relay_min_flip_mm` knob, and the collapse-ramp keys, and references
  the bimodal failure mode

#### Scenario: Commands remain copy-paste verified

- **WHEN** the relay section shows the regenerate/flash loop for the new
  keys
- **THEN** the commands match the scripts exactly (consistent with the
  existing "exact copy-paste commands verified against scripts"
  requirement)
