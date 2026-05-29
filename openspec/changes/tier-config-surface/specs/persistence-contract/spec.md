## MODIFIED Requirements

### Requirement: Runtime Tunables Flow
Any **durable** tunable (tier T1 or T2) SHALL live in `config.ini` and flow
through `gen_config.py`. A T3 internal constant SHALL NOT enter this flow; it
lives in its owning source module (see the `config-surface-tiers` capability)
and is exempt from the config/persist/SET/GET path.

#### Scenario: New Parameter
- **WHEN** a new **durable** (T1/T2) runtime parameter is added
- **THEN** it MUST be represented in `config.ini` and `config.ini.example`
- **AND** consumed in firmware via generated `CONF_*` macros

#### Scenario: New internal constant
- **WHEN** a new T3 internal control-loop constant is introduced
- **THEN** it is defined as `static const` / `#define` in the owning module
- **AND** it is NOT added to `config.ini`, `settings_t`, or the release
  `SET:`/`GET:` surface
