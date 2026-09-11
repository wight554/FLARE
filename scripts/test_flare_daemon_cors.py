#!/usr/bin/env python3
"""Unit tests for flare_daemon.py CORS origin validation and host enforcement."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flare_daemon  # noqa: E402


class FlareDaemonCorsTests(unittest.TestCase):
    def setUp(self):
        flare_daemon.CORS_ALLOW_ALL = False
        flare_daemon.ALLOWED_CORS_ORIGINS = set()

    def test_loopback_origins_allowed_by_default(self):
        self.assertTrue(flare_daemon.is_cors_origin_allowed("http://localhost"))
        self.assertTrue(flare_daemon.is_cors_origin_allowed("http://localhost:8088"))
        self.assertTrue(flare_daemon.is_cors_origin_allowed("http://127.0.0.1"))
        self.assertTrue(flare_daemon.is_cors_origin_allowed("http://127.0.0.1:8088"))
        self.assertTrue(flare_daemon.is_cors_origin_allowed("http://[::1]:8088"))

    def test_external_origins_blocked_by_default(self):
        self.assertFalse(flare_daemon.is_cors_origin_allowed("http://evil.com"))
        self.assertFalse(flare_daemon.is_cors_origin_allowed("http://192.168.1.50:8088"))
        self.assertFalse(flare_daemon.is_cors_origin_allowed("https://attacker.net"))
        self.assertFalse(flare_daemon.is_cors_origin_allowed(""))
        self.assertFalse(flare_daemon.is_cors_origin_allowed(None))

    def test_same_host_origin_allowed(self):
        # When browser accesses http://192.168.1.100:8088 and fetches same host
        self.assertTrue(
            flare_daemon.is_cors_origin_allowed(
                "http://192.168.1.100:8088", request_host="192.168.1.100:8088"
            )
        )
        self.assertTrue(
            flare_daemon.is_cors_origin_allowed(
                "http://flare.local:8088", request_host="flare.local:8088"
            )
        )
        # But an external origin attacking that same host is blocked
        self.assertFalse(
            flare_daemon.is_cors_origin_allowed(
                "http://evil.com", request_host="192.168.1.100:8088"
            )
        )

    def test_explicit_allowed_cors_origins(self):
        flare_daemon.ALLOWED_CORS_ORIGINS = {
            "http://mainsail.local",
            "http://192.168.1.200:80",
        }
        self.assertTrue(flare_daemon.is_cors_origin_allowed("http://mainsail.local"))
        self.assertTrue(flare_daemon.is_cors_origin_allowed("http://192.168.1.200:80"))
        self.assertFalse(flare_daemon.is_cors_origin_allowed("http://192.168.1.99:80"))

    def test_cors_allow_all_mode(self):
        flare_daemon.CORS_ALLOW_ALL = True
        self.assertTrue(flare_daemon.is_cors_origin_allowed("http://evil.com"))
        self.assertTrue(flare_daemon.is_cors_origin_allowed("http://192.168.1.50:8088"))


if __name__ == "__main__":
    unittest.main()
