#!/usr/bin/env python3
"""Regression tripwire for OpenSpec prose compression."""

import re
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SPEC_GLOB = "openspec/specs/**/spec.md"
MAX_FILLER_DENSITY_PCT = 16.0
PURPOSE_HEADING = "## Purpose"
WORD_RE = re.compile(r"[A-Za-z][A-Za-z0-9_-]*")
FILLER_WORDS = {
    "a",
    "an",
    "the",
    "just",
    "really",
    "basically",
    "actually",
    "simply",
}
FILLER_PHRASES = (
    ("in", "order", "to"),
    ("you", "should"),
    ("make", "sure", "to"),
)


@dataclass(frozen=True)
class SpecDensity:
    path: Path
    words: int
    filler: int
    density_pct: float


def purpose_lines(text):
    lines = text.splitlines()
    nonblank = [(idx, line.strip()) for idx, line in enumerate(lines) if line.strip()]
    if not nonblank:
        return None

    first_idx, first_line = nonblank[0]
    if first_line.startswith("# "):
        if len(nonblank) < 2 or nonblank[1][1] != PURPOSE_HEADING:
            return None
        purpose_idx = nonblank[1][0]
    elif first_line == PURPOSE_HEADING:
        purpose_idx = first_idx
    else:
        return None

    body = []
    for line in lines[purpose_idx + 1:]:
        stripped = line.strip()
        if stripped.startswith("#"):
            break
        if stripped:
            body.append(stripped)
    return body


def remove_purpose_section(text):
    lines = text.splitlines()
    output = []
    in_purpose = False
    for line in lines:
        stripped = line.strip()
        if stripped == PURPOSE_HEADING:
            in_purpose = True
            continue
        if in_purpose:
            if stripped.startswith("#"):
                in_purpose = False
            else:
                continue
        output.append(line)
    return "\n".join(output)


def prose_without_protected_regions(text):
    """Return prose counted by the density tripwire."""
    text = remove_purpose_section(text)
    lines = []
    in_fence = False
    for line in text.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if not stripped:
            continue
        if stripped.startswith("#") or stripped.startswith("|"):
            continue
        if line.startswith("    "):
            continue
        lines.append(line)
    return "\n".join(lines)


def words_for_density(text):
    return [match.group(0).lower() for match in WORD_RE.finditer(text)]


def filler_count(words):
    count = 0
    i = 0
    while i < len(words):
        matched_phrase = False
        for phrase in FILLER_PHRASES:
            end = i + len(phrase)
            if tuple(words[i:end]) == phrase:
                count += 1
                i = end
                matched_phrase = True
                break
        if matched_phrase:
            continue
        if words[i] in FILLER_WORDS:
            count += 1
        i += 1
    return count


def density_for_text(path, text):
    prose = prose_without_protected_regions(text)
    words = words_for_density(prose)
    filler = filler_count(words)
    density = (filler / len(words) * 100.0) if words else 0.0
    return SpecDensity(path=Path(path), words=len(words), filler=filler, density_pct=density)


def density_for_path(path):
    path = Path(path)
    return density_for_text(path, path.read_text(encoding="utf-8"))


def specs_under_threshold(paths, threshold=MAX_FILLER_DENSITY_PCT):
    reports = [density_for_path(path) for path in paths]
    return [report for report in reports if report.density_pct > threshold]


def format_offenders(offenders):
    return "\n".join(
        f"{item.path}: {item.density_pct:.2f}% filler "
        f"({item.filler}/{item.words} words)"
        for item in offenders
    )


class TestSpecCompression(unittest.TestCase):
    def test_repository_specs_have_human_purpose(self):
        paths = sorted(REPO.glob(SPEC_GLOB))
        self.assertGreater(len(paths), 0)

        failures = []
        for path in paths:
            lines = purpose_lines(path.read_text(encoding="utf-8"))
            if not lines:
                failures.append(f"{path}: missing ## Purpose")
                continue
            if len(lines) > 3:
                failures.append(f"{path}: Purpose has {len(lines)} lines")
            lowered = " ".join(lines).lower()
            if "tbd" in lowered or "created by archiving" in lowered:
                failures.append(f"{path}: placeholder Purpose")

        self.assertEqual([], failures, "\n".join(failures))

    def test_repository_specs_stay_under_permissive_threshold(self):
        paths = sorted(REPO.glob(SPEC_GLOB))
        self.assertGreater(len(paths), 0)

        offenders = specs_under_threshold(paths)
        self.assertEqual([], offenders, format_offenders(offenders))

    def test_bloated_fixture_fails_tripwire(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "spec.md"
            path.write_text(
                "This is the prose that basically just really actually "
                "uses the filler in order to make sure to fail. " * 8,
                encoding="utf-8",
            )

            offenders = specs_under_threshold([path], threshold=MAX_FILLER_DENSITY_PCT)

        self.assertEqual(1, len(offenders))
        self.assertIn("spec.md", format_offenders(offenders))

    def test_purpose_code_headings_and_tables_are_ignored(self):
        report = density_for_text(
            Path("fixture.md"),
            """\
# Fixture Spec

## Purpose
This is the readable human summary that may use normal prose and should be ignored by the density tripwire.

## Requirements

# The heading should not count the filler

| the | a | an |
| --- | --- | --- |
| just | really | basically |

```c
// the a an just really basically actually simply
int main(void) { return 0; }
```

Compressed prose stays lean. Meaning intact.
""",
        )

        self.assertEqual(6, report.words)
        self.assertEqual(0, report.filler)


def main():
    unittest.main()


if __name__ == "__main__":
    main()
