# FLARE – Git Workflow

Repo drives real hardware. Workflow goals:

- `main` stays buildable and flashable
- experimental work easy to isolate when needed
- each commit leaves clear recovery point

---

## Branch Policy

### `main`

- primary development branch
- expected buildable
- expected flashable on hardware

### Optional short-lived branches

Use branch for risky, long-running, or hardware-specific work.

- `feature/<name>`: new functionality
- `fix/<name>`: bug fixes
- `hw/<name>`: board/pin/timing/hardware experiments

For small validated changes, direct work on `main` is acceptable.

---

## Daily Flow

### 1. Start from current `main`

```bash
git switch main
git pull
```

### 2. Decide whether to branch

Stay on `main` for small contained work, or create branch:

```bash
git switch -c feature/<name>
```

### 3. Validate before every commit

Firmware changes:

```bash
ninja -C build_local
```

Preferred one-command static gate:

```bash
bash scripts/validate_regression.sh
```

If scripts changed:

```bash
python3 -m py_compile scripts/*.py
```

If parameter names, commands, or behavior changed, update docs in same pass.
If new functionality added, analyze affected flows and confirm they remain intact unless change intentionally redefines them.

### 4. Commit with the project format

Use project commit format:

```text
<short description>

<why the change was made>

Generated-By: <Agent Name> (<Model>)
```

Example:

```text
docs: refresh module ownership notes

Replace stale monolithic main.c references in the internal docs so future
sessions start from the current split architecture instead of historical
implementation details.

Generated-By: GitHub Copilot (GPT-5.4)
```

### 5. Push immediately after commit

```bash
git push
```

---

## Restore Points

Use annotated tags for firmware states verified on real hardware.

Create a tag:

```bash
git tag -a baseline_$(date +%Y%m%d)_v1 -m "Verified FLARE hardware baseline"
git push --tags
```

List tags:

```bash
git tag
```

Branch from a known-good baseline:

```bash
git switch -c rescue/from-baseline <tag>
```

---

## Quick Diagnostics

Current branch:

```bash
git branch --show-current
```

Recent commits:

```bash
git log -5 --oneline --decorate
```

Working tree status:

```bash
git status
```
