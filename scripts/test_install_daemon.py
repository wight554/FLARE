#!/usr/bin/env python3
"""Unit tests for install_daemon.py's systemd-unit regeneration.

Focus: a reinstall must not silently drop an operator's manual --host (or
other) customization on the ExecStart line -- the exact regression fixed
2026-06-19 (0.0.0.0 default) and symmetrically possible for a manual
127.0.0.1 override. No systemctl/root interaction: step_systemd_unit()
only reads/writes SERVICE_TEMPLATE/SERVICE_DEST, both monkeypatched here.
"""
import os
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import install_daemon  # noqa: E402


class ExtractExecStartArgsTests(unittest.TestCase):
    def test_no_extra_args(self):
        unit = "[Service]\nExecStart=/usr/bin/python3 /opt/flare/scripts/flare_daemon.py\n"
        self.assertEqual(install_daemon._extract_execstart_extra_args(unit), "")

    def test_host_arg_preserved(self):
        unit = ("[Service]\nExecStart=/usr/bin/python3 /opt/flare/scripts/flare_daemon.py "
                "--host 127.0.0.1\n")
        self.assertEqual(
            install_daemon._extract_execstart_extra_args(unit), "--host 127.0.0.1")

    def test_multiple_args_preserved(self):
        unit = ("[Service]\nExecStart=/usr/bin/python3 /opt/flare/scripts/flare_daemon.py "
                "--host 127.0.0.1 --api-port 9000\n")
        self.assertEqual(
            install_daemon._extract_execstart_extra_args(unit),
            "--host 127.0.0.1 --api-port 9000")

    def test_no_execstart_line(self):
        self.assertEqual(install_daemon._extract_execstart_extra_args("[Service]\n"), "")


class StepSystemdUnitReinstallTests(unittest.TestCase):
    def setUp(self):
        self._tmp = TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        tmp = Path(self._tmp.name)

        self._orig_template = install_daemon.SERVICE_TEMPLATE
        self._orig_dest = install_daemon.SERVICE_DEST
        install_daemon.SERVICE_TEMPLATE = tmp / "flare_daemon.service"
        install_daemon.SERVICE_DEST = tmp / "installed.service"
        install_daemon.SERVICE_TEMPLATE.write_text(
            "[Unit]\nDescription=x\n\n[Service]\nUser={{USER}}\n"
            "WorkingDirectory={{DIR}}\n"
            "ExecStart=/usr/bin/python3 {{DIR}}/scripts/flare_daemon.py\n"
            "Restart=always\n\n[Install]\nWantedBy=multi-user.target\n"
        )

        def _noop_chmod(*a, **k):
            pass
        self._orig_chmod = os.chmod
        os.chmod = _noop_chmod

    def tearDown(self):
        install_daemon.SERVICE_TEMPLATE = self._orig_template
        install_daemon.SERVICE_DEST = self._orig_dest
        os.chmod = self._orig_chmod

    def test_fresh_install_has_no_extra_args(self):
        install_daemon.step_systemd_unit("pi")
        text = install_daemon.SERVICE_DEST.read_text()
        line = next(ln for ln in text.splitlines() if ln.startswith("ExecStart="))
        self.assertTrue(line.endswith("flare_daemon.py"), line)

    def test_reinstall_preserves_manual_host_override(self):
        # Simulate a prior install that was manually edited to add --host.
        install_daemon.step_systemd_unit("pi")
        text = install_daemon.SERVICE_DEST.read_text()
        text = text.replace(
            "flare_daemon.py\n", "flare_daemon.py --host 127.0.0.1\n")
        install_daemon.SERVICE_DEST.write_text(text)

        # Reinstall (regenerate from template) must not drop the override.
        install_daemon.step_systemd_unit("pi")
        text = install_daemon.SERVICE_DEST.read_text()
        line = next(ln for ln in text.splitlines() if ln.startswith("ExecStart="))
        self.assertTrue(line.endswith("--host 127.0.0.1"), line)

    def test_reinstall_with_no_prior_customization_stays_plain(self):
        install_daemon.step_systemd_unit("pi")
        install_daemon.step_systemd_unit("pi")  # reinstall, nothing to preserve
        text = install_daemon.SERVICE_DEST.read_text()
        line = next(ln for ln in text.splitlines() if ln.startswith("ExecStart="))
        self.assertTrue(line.endswith("flare_daemon.py"), line)


if __name__ == "__main__":
    unittest.main()
