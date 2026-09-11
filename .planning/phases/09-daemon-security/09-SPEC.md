# Phase 9: Daemon Security & Remote Command Hardening — Specification (09-SPEC.md)

## Problem Statement
When `scripts/flare_daemon.py` is bound to `--host 0.0.0.0` to permit LAN monitoring (e.g. Fluidd/Mainsail or web dashboard on mobile/desktop), any client on the network can issue unauthenticated HTTP requests to mutating endpoints:
- `POST /cmd`: Can send arbitrary serial commands (`CUT:`, `FEED:`, `REV:`, `SV:`, `RS:`, `FLASH:`), potentially damaging the physical printer or corrupting RP2040 flash.
- `POST /config` and `POST /gatemap`: Can alter gate mapping and filament bypass parameters.
- Rapid unconstrained HTTP requests can flood the USB CDC serial ringbuffer, causing buffer overflows or serial thread starvation.

## Requirements & Design Decisions

### 1. Trust Boundary & Loopback Exemption
- **Loopback Exemption**: Peer IP (`client_address[0]`) in `('127.0.0.1', '::1', 'localhost')` is completely exempt from authentication and remote rate limits. Zero-friction for Klipper macros, local Moonraker integration, and local `flare_cmd.py`.
- **Anti-Spoofing**: `X-Forwarded-For` and `Forwarded` headers are ignored by default. Peer IP is taken strictly from socket `client_address[0]`.
- **Proxy Flag**: An optional `--trust-proxy` command-line flag enables reading the leftmost IP from `X-Forwarded-For` when `flare_daemon` is deployed behind a trusted local reverse proxy (Nginx / Moonraker).

### 2. Authorization & Bearer Token Management
- **Token Discovery Order**:
  1. CLI argument: `--auth-token <SECRET>`
  2. Environment variable: `FLARE_AUTH_TOKEN`
  3. Token file: `~/.flare/auth.token`
  4. Auto-generation: If bound to a non-loopback host (e.g. `0.0.0.0`) and no token is found, daemon auto-generates a secure 32-character hexadecimal token (`secrets.token_hex(16)`), writes it to `~/.flare/auth.token` with mode `0600`, and logs the path.
- **Local-Only Exemption**: If daemon is bound strictly to `127.0.0.1`, auth enforcement is disabled by default.

### 3. Endpoint Authorization Matrix
- **Public Read (LAN & Local)**:
  - `GET /`
  - `GET /index.html`
  - `GET /app.js`
  - `GET /style.css`
  - `GET /status`
  - `GET /telemetry` (SSE stream)
  - `GET /config`
  - `GET /gatemap`
  Returns `200 OK` without requiring authentication headers.
- **Protected Mutations (LAN Requires Bearer Token)**:
  - `POST /cmd`
  - `POST /config`
  - `POST /gatemap`
  If client is non-loopback and header `Authorization: Bearer <token>` is missing or invalid:
  Returns `401 Unauthorized` with JSON body `{"error": "unauthorized"}`.
- **Full Privilege**: Once authenticated with valid token, remote caller has full command execution rights.

### 4. Rate Limiting & DoS Mitigation
- **Token Bucket Limiter**: Applied to `POST /cmd` per remote client IP.
  - Rate: 10 requests per second.
  - Burst capacity: 20 tokens.
  - Periodic garbage collection of inactive client buckets (>60s idle).
- **HTTP Response**: Returns `429 Too Many Requests` with JSON body `{"error": "rate_limit_exceeded"}` and `Retry-After: 1` header.
- **Loopback Exemption**: Loopback IP (`127.0.0.1` / `::1`) is exempt from the remote rate limiter to ensure Klipper toolchange and sync macros never stall.

### 5. Client & UI Integration
- **CLI (`scripts/flare_cmd.py`)**:
  - Adds `--api-host` (default: `127.0.0.1`) and `--auth-token` options.
  - Automatically loads `~/.flare/auth.token` if target is non-loopback and token not explicitly supplied.
  - Injects `Authorization: Bearer <token>` header into HTTP requests.
- **WebUI (`scripts/webui/app.js` / `index.html`)**:
  - Stores API token in `localStorage.getItem('flare_api_token')`.
  - Injects `Authorization: Bearer <token>` header into `fetch('/cmd')` and `fetch('/gatemap')` requests.
  - Displays token entry prompt/modal when 401 Unauthorized is encountered, saving valid token to `localStorage`.
  - Adds API Key settings button to top bar.

### 6. Testing & Verification
- Unit test suite: `scripts/test_flare_daemon_security.py` verifying:
  - Token file generation and secure permissions (`0600`).
  - Loopback authentication exemption.
  - Remote GET endpoint open access.
  - Remote POST endpoint 401 rejection on missing/invalid token.
  - Remote POST endpoint 200 success on valid Bearer token.
  - Token-bucket rate limiting (429 Too Many Requests on burst exceed).
  - Rate limiting loopback exemption.
  - Proxy header handling with and without `--trust-proxy`.
