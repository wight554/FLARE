#pragma once
// =====================================================================
// FLARE Tier-3 internal control-loop constants.
//
// These are NOT user-tunable: no documented operator procedure, set once
// against the algorithm. They are intentionally absent from config.ini,
// from settings_t persistence, and from the release-build SET:/GET: surface.
// To change one: edit the value here and recompile (no SETTINGS_VERSION bump).
//
// A build configured with -DFLARE_DEV_TUNING=ON re-exposes some of these as
// ephemeral SET: overrides (runtime-only, not persisted — revert on reboot)
// for bench experimentation. See the `tier-config-surface` change.
//
// Only unit-independent constants live here. Tier-3 values that require the
// motor's mm_per_step conversion (rates/accels) remain compile-time CONF_*
// macros generated into tune.h by gen_config.py.
// =====================================================================

// ----- Estimator confidence thresholds -----
#define FLARE_INT_EST_LOW_CF_WARN_THRESHOLD 0.5f // EV:BUF,EST_LOW_CF fires when confidence < this
#define FLARE_INT_EST_FALLBACK_CF_THRESHOLD 0.2f // integral freezes when confidence < this

// ----- TENSION-risk telemetry -----
#define FLARE_INT_TENSION_RISK_WINDOW_MS                                                           \
    60000                                  // rolling window for TENSION-pin density warning (ms)
#define FLARE_INT_TENSION_RISK_THRESHOLD 4 // EV:SYNC,TENSION_RISK_HIGH fires when pin count >= this

// ----- Drift observer (per-transition residual BPD correction) -----
#define FLARE_INT_BUF_DRIFT_EWMA_TAU_MS 60000 // EWMA time constant (ms)
#define FLARE_INT_BUF_DRIFT_MIN_SAMPLES 3     // samples before full correction
#define FLARE_INT_BUF_DRIFT_APPLY_THR_MM 2.0f // correction threshold (mm); 0 disables
#define FLARE_INT_BUF_DRIFT_CLAMP_MM 3.0f     // hard clamp on applied correction (mm)
#define FLARE_INT_BUF_DRIFT_APPLY_MIN_CF 0.5f // min estimator confidence to apply

// ----- Sync tension guards -----
#define FLARE_INT_SYNC_TENSION_DWELL_STOP_MS 6000 // hard stop if pinned at TENSION this long; 0=off
#define FLARE_INT_SYNC_TENSION_RAMP_DELAY_MS 0    // estimator-bypass refill ramp delay

// ----- Sync overshoot / reserve integral -----
#define FLARE_INT_SYNC_OVERSHOOT_NEUTRAL_EXTEND 1 // extend compression overshoot trim into NEUTRAL
#define FLARE_INT_SYNC_RESERVE_INTEGRAL_GAIN 0.0f
#define FLARE_INT_SYNC_RESERVE_INTEGRAL_CLAMP_MM 0.6f
#define FLARE_INT_SYNC_RESERVE_INTEGRAL_DECAY_MS 0

// ----- Estimator sigma hard cap -----
#define FLARE_INT_EST_SIGMA_HARD_CAP_MM 1.5f // confidence→0 at this sigma (≈ buffer half-travel)

// ----- Analog PSF derivative gain -----
#define FLARE_INT_KD_PSF 0.0f

// ----- Relay collapse law (type-D deep-COMPRESSION ramp) -----
#define FLARE_INT_RELAY_MIN_FLIP_MM 0.0f // BLOCKED non-zero: see relay-min-flip deadlock
#define FLARE_INT_RELAY_COLLAPSE_DELAY_MS 250
#define FLARE_INT_RELAY_COLLAPSE_RAMP_MULT 3
#define FLARE_INT_RELAY_COLLAPSE_CAP_MS 600

// ----- Neutral creep -----
#define FLARE_INT_NEUTRAL_CREEP_TIMEOUT_MS 4000
#define FLARE_INT_NEUTRAL_CREEP_RATE_SPS_PER_S 5
#define FLARE_INT_NEUTRAL_CREEP_CAP_FRAC 10

// ----- Variance-aware blend -----
#define FLARE_INT_BUF_VARIANCE_BLEND_FRAC 0.5f   // max variance distrust blend (0=off)
#define FLARE_INT_BUF_VARIANCE_BLEND_REF_MM 1.0f // sigma at which distrust saturates

// ----- Estimator responsiveness (EWMA alpha bounds) -----
#define FLARE_INT_EST_ALPHA_MIN 0.12f  // slow-drift responsiveness
#define FLARE_INT_EST_ALPHA_MAX 0.65f  // sharp-jump responsiveness
#define FLARE_INT_BASELINE_ALPHA 0.02f // baseline EWMA smoothing

// ----- Loop quantization / debounce -----
// (sync_tick_ms / ramp_tick_ms stay CONF_* — gen_config uses them in the
//  accel→sps ramp conversions, so the compile math and runtime tick must agree.)
#define FLARE_INT_BUF_HYST_MS 30         // switch debounce window
#define FLARE_INT_BUF_PREDICT_THR_MS 250 // predictive crossing threshold

// ----- Sync overshoot trim -----
#define FLARE_INT_SYNC_OVERSHOOT_PCT 150 // trailing-side trim (% of correction)

// ----- Analog PSF input filter -----
#define FLARE_INT_BUF_ANALOG_ALPHA 0.20f // analog buffer position EWMA

// ----- Watchdog timeouts (padded for worst-case slow hardware; trip => abort a
//        stuck op, so they only need to exceed the slowest legitimate duration) -----
#define FLARE_INT_CUT_TIMEOUT_FEED_MS 30000 // feed-to-cutter (200mm @ sane rate << 30s)
#define FLARE_INT_CUT_TIMEOUT_SETTLE_MS                                                            \
    5000 // per servo-settle step (>= SERVO_SETTLE clamp 2000 + margin)
#define FLARE_INT_TC_TIMEOUT_CUT_MS                                                                \
    8000 // outer cut watchdog floor (firmware extends to cut duration)
#define FLARE_INT_TC_TIMEOUT_TH_MS 8000     // toolhead-clear on TC unload (slow retract margin)
#define FLARE_INT_TC_TIMEOUT_Y_MS 12000     // tail clears Y-split (long bowden + slow unload)
#define FLARE_INT_RELOAD_Y_TIMEOUT_MS 15000 // Y-clear during RELOAD

// ----- Misc internal timing -----
#define FLARE_INT_MOTION_STARTUP_MS 1000     // lane motion startup window
#define FLARE_INT_POST_PRINT_STAB_DELAY_MS 0 // idle-compression negative-sync delay
#define FLARE_INT_SYNC_PSF_DECAY_SPS_PER_S                                                         \
    25000.0f // type-P feed wall-clock decay on demand-drop (anti-overfeed)
#define FLARE_INT_TS_BUF_FALLBACK_MS 2000  // toolhead-sensor buffer fallback window
#define FLARE_INT_RELOAD_LEAN_FACTOR 1.15f // RELOAD follow over-feed factor
