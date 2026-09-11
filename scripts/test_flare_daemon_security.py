#!/usr/bin/env python3
"""Unit tests for flare_daemon.py authentication, loopback exemption, and rate limiting."""
import http.client
import json
import os
import shutil
import stat
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flare_daemon  # noqa: E402


class DummyHandler(flare_daemon.BaseHTTPRequestHandler):
    def __init__(self, client_ip="192.168.1.100", headers=None):
        self.client_address = (client_ip, 54321)
        self.headers = headers or {}


class FlareDaemonSecurityTests(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.token_file = os.path.join(self.test_dir, "auth.token")
        self._orig_get_token_path = flare_daemon.get_default_token_path
        flare_daemon.get_default_token_path = lambda: self.token_file

        flare_daemon.AUTH_TOKEN = None
        flare_daemon.AUTH_REQUIRED = False
        flare_daemon.TRUST_PROXY = False
        with flare_daemon.RATE_LIMIT_LOCK:
            flare_daemon.RATE_LIMIT_BUCKETS.clear()

    def tearDown(self):
        flare_daemon.get_default_token_path = self._orig_get_token_path
        flare_daemon.AUTH_TOKEN = None
        flare_daemon.AUTH_REQUIRED = False
        flare_daemon.TRUST_PROXY = False
        with flare_daemon.RATE_LIMIT_LOCK:
            flare_daemon.RATE_LIMIT_BUCKETS.clear()
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_loopback_detection(self):
        self.assertTrue(flare_daemon.is_loopback("127.0.0.1"))
        self.assertTrue(flare_daemon.is_loopback("127.0.1.1"))
        self.assertTrue(flare_daemon.is_loopback("localhost"))
        self.assertTrue(flare_daemon.is_loopback("::1"))
        self.assertTrue(flare_daemon.is_loopback("::ffff:127.0.0.1"))

        self.assertFalse(flare_daemon.is_loopback("192.168.1.50"))
        self.assertFalse(flare_daemon.is_loopback("10.0.0.5"))
        self.assertFalse(flare_daemon.is_loopback("example.com"))
        self.assertFalse(flare_daemon.is_loopback(""))
        self.assertFalse(flare_daemon.is_loopback(None))

    def test_client_ip_anti_spoofing(self):
        handler = DummyHandler("192.168.1.100", {"X-Forwarded-For": "127.0.0.1"})
        # By default, TRUST_PROXY is False -> ignores X-Forwarded-For
        self.assertEqual(flare_daemon.get_client_ip(handler), "192.168.1.100")

        # When TRUST_PROXY is True, evaluates first IP
        flare_daemon.TRUST_PROXY = True
        self.assertEqual(flare_daemon.get_client_ip(handler), "127.0.0.1")

    def test_token_auto_generation_and_permissions(self):
        # When bound to non-loopback host, auto-generates token with 0600 permissions
        token = flare_daemon.init_auth_token("0.0.0.0")
        self.assertTrue(flare_daemon.AUTH_REQUIRED)
        self.assertIsNotNone(token)
        self.assertEqual(len(token), 32)
        self.assertTrue(os.path.exists(self.token_file))

        file_stat = os.stat(self.token_file)
        file_mode = stat.S_IMODE(file_stat.st_mode)
        self.assertEqual(file_mode, 0o600)

        # Ensure subsequent load reuses the token
        token2 = flare_daemon.init_auth_token("0.0.0.0")
        self.assertEqual(token, token2)

    def test_loopback_disables_auth_requirement(self):
        flare_daemon.init_auth_token("127.0.0.1")
        self.assertFalse(flare_daemon.AUTH_REQUIRED)

    def test_is_request_authenticated_matrix(self):
        flare_daemon.init_auth_token("0.0.0.0", cli_token="secret-key-12345")
        self.assertTrue(flare_daemon.AUTH_REQUIRED)

        # Loopback caller is always authenticated without token
        local_handler = DummyHandler("127.0.0.1")
        self.assertTrue(flare_daemon.is_request_authenticated(local_handler))

        # Remote caller without token is rejected
        remote_no_auth = DummyHandler("192.168.1.50")
        self.assertFalse(flare_daemon.is_request_authenticated(remote_no_auth))

        # Remote caller with invalid token is rejected
        remote_bad_auth = DummyHandler("192.168.1.50", {"Authorization": "Bearer wrong-key"})
        self.assertFalse(flare_daemon.is_request_authenticated(remote_bad_auth))

        # Remote caller with valid Bearer token is accepted
        remote_good_auth = DummyHandler("192.168.1.50", {"Authorization": "Bearer secret-key-12345"})
        self.assertTrue(flare_daemon.is_request_authenticated(remote_good_auth))

    def test_rate_limiter_bucket_and_loopback_exemption(self):
        bucket = flare_daemon.TokenBucket(rate=10.0, capacity=20.0)
        # Should consume 20 burst tokens
        for _ in range(20):
            self.assertTrue(bucket.consume())
        # 21st in instant burst must fail
        self.assertFalse(bucket.consume())

        # Loopback check_rate_limit is always True
        for _ in range(50):
            self.assertTrue(flare_daemon.check_rate_limit("127.0.0.1"))

        # Remote IP check_rate_limit exhausts after burst capacity
        remote_ip = "192.168.1.75"
        for _ in range(20):
            self.assertTrue(flare_daemon.check_rate_limit(remote_ip))
        self.assertFalse(flare_daemon.check_rate_limit(remote_ip))


class FlareDaemonHTTPSecurityIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server_port = 18089
        cls.server_host = "127.0.0.1"
        cls.token = "integration-test-token-abcdef"

        flare_daemon.CORS_ALLOW_ALL = True
        flare_daemon.AUTH_REQUIRED = True
        flare_daemon.AUTH_TOKEN = cls.token

        cls.server = flare_daemon.ThreadedHTTPServer((cls.server_host, cls.server_port), flare_daemon.FlareHTTPHandler)
        cls.server_thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.server_thread.start()
        time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.server_thread.join(timeout=2.0)

    def setUp(self):
        with flare_daemon.RATE_LIMIT_LOCK:
            flare_daemon.RATE_LIMIT_BUCKETS.clear()

    def test_cors_options_allows_authorization_header(self):
        conn = http.client.HTTPConnection(self.server_host, self.server_port)
        conn.request("OPTIONS", "/cmd", headers={"Origin": "http://example.com"})
        resp = conn.getresponse()
        self.assertEqual(resp.status, 200)
        allow_headers = resp.getheader("Access-Control-Allow-Headers", "")
        self.assertIn("Authorization", allow_headers)
        self.assertIn("Content-Type", allow_headers)
        conn.close()

    def test_unauthenticated_get_status_allowed_from_anywhere(self):
        conn = http.client.HTTPConnection(self.server_host, self.server_port)
        conn.request("GET", "/status")
        resp = conn.getresponse()
        self.assertEqual(resp.status, 200)
        data = json.loads(resp.read().decode("utf-8"))
        self.assertIn("board_online", data)
        conn.close()

    def test_post_cmd_loopback_exempt_from_auth(self):
        # Request comes over 127.0.0.1 loopback socket -> no token needed
        conn = http.client.HTTPConnection(self.server_host, self.server_port)
        payload = json.dumps({"cmd": "V:"})
        conn.request("POST", "/cmd", body=payload, headers={"Content-Type": "application/json"})
        resp = conn.getresponse()
        # When serial port is None, daemon returns ER:BOARD_OFFLINE with 200 OK
        self.assertEqual(resp.status, 200)
        data = json.loads(resp.read().decode("utf-8"))
        self.assertEqual(data.get("response"), "ER:BOARD_OFFLINE")
        conn.close()

    def test_remote_post_cmd_requires_auth_and_blocks_unauthorized(self):
        flare_daemon.TRUST_PROXY = True
        try:
            conn = http.client.HTTPConnection(self.server_host, self.server_port)
            payload = json.dumps({"cmd": "V:"})

            # 1. Remote without token -> 401
            conn.request("POST", "/cmd", body=payload, headers={
                "Content-Type": "application/json",
                "X-Forwarded-For": "192.168.1.50"
            })
            resp = conn.getresponse()
            self.assertEqual(resp.status, 401)
            body = json.loads(resp.read().decode("utf-8"))
            self.assertEqual(body.get("error"), "unauthorized")

            # 2. Remote with wrong token -> 401
            conn.request("POST", "/cmd", body=payload, headers={
                "Content-Type": "application/json",
                "X-Forwarded-For": "192.168.1.50",
                "Authorization": "Bearer bad-token"
            })
            resp = conn.getresponse()
            self.assertEqual(resp.status, 401)

            # 3. Remote with valid token -> 200
            conn.request("POST", "/cmd", body=payload, headers={
                "Content-Type": "application/json",
                "X-Forwarded-For": "192.168.1.50",
                "Authorization": f"Bearer {self.token}"
            })
            resp = conn.getresponse()
            self.assertEqual(resp.status, 200)
            conn.close()
        finally:
            flare_daemon.TRUST_PROXY = False

    def test_remote_rate_limiting_returns_429(self):
        flare_daemon.TRUST_PROXY = True
        try:
            conn = http.client.HTTPConnection(self.server_host, self.server_port)
            payload = json.dumps({"cmd": "V:"})
            headers = {
                "Content-Type": "application/json",
                "X-Forwarded-For": "192.168.1.99",
                "Authorization": f"Bearer {self.token}"
            }

            # First 20 should succeed (burst capacity)
            statuses = []
            for _ in range(25):
                conn.request("POST", "/cmd", body=payload, headers=headers)
                resp = conn.getresponse()
                resp.read()
                statuses.append(resp.status)

            self.assertEqual(statuses[:20], [200] * 20)
            self.assertEqual(statuses[20], 429)
            conn.close()
        finally:
            flare_daemon.TRUST_PROXY = False

    def test_flare_cmd_integration_with_auth(self):
        import flare_cmd

        orig_url = flare_cmd.DAEMON_URL
        orig_token = flare_cmd.DAEMON_AUTH_TOKEN
        flare_cmd.DAEMON_URL = f"http://{self.server_host}:{self.server_port}"
        flare_cmd.DAEMON_AUTH_TOKEN = self.token

        try:
            # Status check
            status = flare_cmd.get_daemon_status()
            self.assertIsNotNone(status)

            # Command execution
            res = flare_cmd.send_daemon_cmd("V:")
            self.assertEqual(res, "ER:BOARD_OFFLINE")
        finally:
            flare_cmd.DAEMON_URL = orig_url
            flare_cmd.DAEMON_AUTH_TOKEN = orig_token


class FlareCmdTokenResolutionTests(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.token_file = os.path.join(self.test_dir, "auth.token")
        self.orig_home = os.environ.get("HOME")
        os.environ["HOME"] = self.test_dir
        flare_dir = os.path.join(self.test_dir, ".flare")
        os.makedirs(flare_dir, exist_ok=True)
        self.token_file_flare = os.path.join(flare_dir, "auth.token")

        if "FLARE_AUTH_TOKEN" in os.environ:
            del os.environ["FLARE_AUTH_TOKEN"]

    def tearDown(self):
        if self.orig_home:
            os.environ["HOME"] = self.orig_home
        if "FLARE_AUTH_TOKEN" in os.environ:
            del os.environ["FLARE_AUTH_TOKEN"]
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_token_resolution_order(self):
        import flare_cmd

        # 1. No token anywhere -> None
        self.assertIsNone(flare_cmd.get_auth_token())

        # 2. File exists -> loads file
        with open(self.token_file_flare, "w", encoding="utf-8") as f:
            f.write("file-token-123\n")
        self.assertEqual(flare_cmd.get_auth_token(), "file-token-123")

        # 3. Environment variable overrides file
        os.environ["FLARE_AUTH_TOKEN"] = "env-token-456"
        self.assertEqual(flare_cmd.get_auth_token(), "env-token-456")

        # 4. CLI explicit arg overrides environment
        self.assertEqual(flare_cmd.get_auth_token("cli-token-789"), "cli-token-789")


if __name__ == "__main__":
    unittest.main()
