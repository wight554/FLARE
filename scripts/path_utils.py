#!/usr/bin/env python3
"""Shared path handling utilities for FLARE host scripts."""

import glob
import os


class PathError(Exception):
    """Raised when a path argument cannot be resolved or is invalid."""
    def __init__(self, path, reason):
        self.path = path
        self.reason = reason
        super().__init__(f"{path}: {reason}")


def normalize_output(path):
    """Expand ~ in output paths. Never glob."""
    if not path:
        return path
    return os.path.expanduser(path)


def resolve_input(pattern, must_exist=True):
    """
    Resolve a single input path, supporting ~ and globs.
    Returns exactly one match or raises PathError.
    """
    if not pattern:
        return pattern

    path = os.path.expanduser(pattern)
    if glob.has_magic(path):
        matches = sorted(glob.glob(path, recursive=True))
        if not matches:
            raise PathError(pattern, "no files matched pattern")
        if len(matches) > 1:
            raise PathError(pattern, f"ambiguous match; matches {len(matches)} files")
        resolved = matches[0]
    else:
        resolved = path

    if must_exist:
        if not os.path.exists(resolved):
            raise PathError(pattern, "file not found")
        if not os.path.isfile(resolved):
            raise PathError(pattern, "not a regular file")
        if not os.access(resolved, os.R_OK):
            raise PathError(pattern, "permission denied")

    return resolved


def expand_input_paths(patterns):
    """
    Expand a list of patterns (or a single pattern) into a sorted, unique list
    of existing regular files. Supports ~ and globs.
    Raises PathError if a literal path is missing or a glob matches nothing.
    """
    if patterns is None:
        return []
    if isinstance(patterns, str):
        patterns = [patterns]

    resolved_paths = set()
    for pattern in patterns:
        path = os.path.expanduser(pattern)
        if glob.has_magic(path):
            matches = glob.glob(path, recursive=True)
            if not matches:
                raise PathError(pattern, "no files matched pattern")
            for m in matches:
                if os.path.isfile(m):
                    resolved_paths.add(m)
        else:
            if not os.path.exists(path):
                raise PathError(pattern, "file not found")
            if not os.path.isfile(path):
                raise PathError(pattern, "not a regular file")
            if not os.access(path, os.R_OK):
                raise PathError(pattern, "permission denied")
            resolved_paths.add(path)

    return sorted(list(resolved_paths))
