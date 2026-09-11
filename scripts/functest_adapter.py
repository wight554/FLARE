#!/usr/bin/env python3
"""Expose a script's module-level ``test_*`` functions to ``unittest`` discovery.

Several host test scripts predate the unittest convention and run their cases from
a hand-written ``main()`` table. ``unittest discover`` silently skips modules with
no ``TestCase`` (audit-hardening-fixes gotcha; docs-test-overhaul D1 requires the
gate to use discovery, not hard-coded invocations). Appending

    FunctionTests = functest_adapter.testcase_from_module(globals())

to such a script turns every ``test_*`` function into a ``TestCase`` method while
leaving the standalone ``main()`` runner untouched.
"""
import inspect
import unittest


def testcase_from_module(namespace, class_name="FunctionTests"):
    """Build a ``unittest.TestCase`` whose methods call each ``test_*`` function."""
    functions = {
        name: fn
        for name, fn in namespace.items()
        if name.startswith("test_") and inspect.isfunction(fn) and not inspect.signature(fn).parameters
    }
    attrs = {name: (lambda self, fn=fn: fn()) for name, fn in functions.items()}
    for name, fn in functions.items():
        attrs[name].__doc__ = fn.__doc__
    cls = type(class_name, (unittest.TestCase,), attrs)
    cls.__module__ = namespace.get("__name__", __name__)
    return cls


def testcase_from_callable(runner, class_name="RunnerTests", method_name="test_runner"):
    """Wrap a whole-suite runner (``main()``/``run_tests()``) that signals failure by
    raising or by ``sys.exit`` with a non-zero status."""

    def method(self):
        try:
            runner()
        except SystemExit as exc:  # runners that exit(0/1)
            self.assertIn(exc.code, (None, 0), f"{runner.__name__} exited with {exc.code}")

    cls = type(class_name, (unittest.TestCase,), {method_name: method})
    cls.__module__ = runner.__module__
    return cls
