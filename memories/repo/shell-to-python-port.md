# shell-to-python-port (archived 2026-06-10)

- All shell scripts ported to Python (stdlib + pyserial only), shell wrappers deleted; `scripts/` is single-language now.
- Regression suite (`scripts/validate_regression.py`) re-run as acceptance before archive.
- Constraint held: no new host-tool dependencies; cross-platform path handling per `script-path-handling` spec.
