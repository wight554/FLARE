# Proposal: TMC2209 Register Heartbeat and Recovery

## Why
Trinamic TMC2209 stepper registers (`IHOLD_IRUN`, `CHOPCONF`, `PWMCONF`, `TPWMTHRS`) are completely volatile. If the 24V motor power rail experiences a momentary brownout, thermal trip, or supply drop, the driver resets to silicon defaults (analog VREF scaling, default microstepping, stealthChop off). The MCU has no periodic heartbeat checking driver state, leading to motor stalling or overheating.

## What Changes
- Add a periodic low-frequency register verification check (e.g. every 5000 ms during idle) in `firmware/src/main.c`.
- Read a sentinel register (`GCONF` or `CHOPCONF`) via `tmc_read()`.
- If register values mismatch active settings or driver reports reset flag, re-apply the full register configuration and emit `EV:TMC:RESTORED`.

## Impact
- `firmware/src/main.c`
- `firmware/src/tmc2209.c`
- `firmware/include/controller_shared.h`
