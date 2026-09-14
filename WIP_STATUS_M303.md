# WIP status: M303 autotune for LpcHeater (issue #19)

Scratch file for resuming this work if the session is interrupted. Delete before opening/merging the PR.

Branch: `lpc-heater-autotune` (based off `origin/main`, NOT `restore-checksum-fixes`).
Issue: https://github.com/itsyumiki/RepRapFirmware-for-da-vinci-jr-1.0/issues/19

## Design decisions already made (don't redo this thinking)

- Tuning report is split into **two** LPC frames, `heaterTuningReportA` + `heaterTuningReportB`,
  sent back-to-back, no sequence tag/pairing window. Justification: point-to-point UART link,
  a report only fires once per completed tuning cycle (many seconds apart), nothing else can
  interleave. Full precision preserved (uint32 ms fields, float rate/voltage fields) - no
  fixed-point compromise needed since two 16-byte frames comfortably hold the CAN struct's
  30 bytes of payload once the redundant `heater` field is dropped (LPC only ever has one heater).
- Cycle-completion detection on the SAM4E side and the "should I send a report" check on the LPC
  side both use a **plain cycle-count comparison** (`tuningCyclesDone != tuningReportedCycle`),
  not a dirty-flag-with-reset-on-read. Simpler, no flag-pairing bugs possible.
- `LpcHeater` has no CAN address/dispatcher push mechanism like `RemoteHeater` does, so it **polls**
  `LpcInterface::GetTuningReport()` from its own `Spin()` instead of receiving a pushed callback.
- Relay-tuning state machine in `LpcFirmware/src/Thermal.cpp` (`StartTuning`/`StopTuning`/`DoTuningStep`)
  is a direct port of RRF's own `LocalHeater::DoTuningStep()` `ExpansionMode` branches (read from
  `RepRapFirmware/src/Heating/LocalHeater.cpp` around lines 627-900) - that's the authoritative
  reference for ton/toff/dhigh/dlow/heatingRate/coolingRate semantics and the peakTempDrop-based
  peak/trough detection. Do not reinvent this logic; copy the reference semantics.
- Cancellation (`on=0` in `heaterTuningCommand`, or a normal `HeaterCommand::off/suspend/resetFault`
  arriving mid-tune) must ALWAYS land in `HeaterState::off`, never `fault`. This is implemented via
  `StopTuning()` and by clearing `tuningPhase` in the `off`/`suspend`/`resetFault` branches of
  `Thermal::Command()`.
- `MaxTuningCycles = 5` in `Thermal.cpp` (single fixed cycle count, no consistency-based early exit
  like `RemoteHeater`/`LocalHeater` do with `MinTuningHeaterCycles`/`isConsistent` - out of scope
  per the issue, which only asks for hotend H0, no multi-phase fan tuning).
- No CRP/Version/thermistorConfig payload changes - explicitly out of scope per the issue (past
  incident referenced in PR #15).

## Files changed so far (all done, believed correct, NOT YET BUILD-TESTED)

1. `Shared/src/LpcProtocol.h` - added `heaterTuningCommand=17`, `heaterTuningReportA=18`,
   `heaterTuningReportB=19` + documented byte layouts in comments above the enum. DONE.
2. `LpcFirmware/src/Thermal.h` - added `TakeTuningReport()` decl. DONE.
3. `LpcFirmware/src/Thermal.cpp` - DONE:
   - New statics: `TuningPhase` enum, `tuningPwm/tuningLowTemp/tuningHighTemp/tuningPeakTempDrop`,
     `tuningExtremeTemp/tuningExtremeTime/tuningAfterExtremeTime/tuningPhaseStartTime`,
     `tuningCyclesDone/tuningReportedCycle`, `tuningLastTon/Toff/DLow/DHigh/HeatingRate/CoolingRate`.
   - `PutU16`/`PutU32`/`PutFloat` helpers added (mirroring LpcInterface.cpp's existing style).
   - `StopTuning()`, `StartTuning(pwm, lowTemp, highTemp, peakTempDrop)`, `DoTuningStep(now)` added
     before `Control()`.
   - `Control()`: tuning branch inserted after the shared LinkAlive/bad-reading/over-temp/under-temp
     checks, before the PID `switch(state)` block - tuning fully bypasses PID/bang-bang.
   - `Command()`: `off`/`suspend`/`resetFault` branches now also clear `tuningPhase`.
   - `ConfigureHeaterTuning()` handler added, wired into `HandleFrame()` switch for
     `heaterTuningCommand`.
   - `TakeTuningReport(payloadA, lengthA, payloadB, lengthB)` added after `TakeStatus()`.
4. `LpcFirmware/src/main.cpp` - DONE: send loop calls `Thermal::TakeTuningReport` and sends both
   frames back-to-back when a new cycle completed.
5. `RepRapFirmware/src/Hardware/SAM4E/LpcInterface.h` - DONE: added `TuningReport` struct,
   `StartHeaterTuning(bool on, float pwm, float lowTemp, float highTemp, float peakTempDrop)` decl,
   `GetTuningReport(TuningReport&)` decl.
6. `RepRapFirmware/src/Hardware/SAM4E/LpcInterface.cpp` - PARTIALLY DONE:
   - Added `ReadU32`/`ReadFloat` helpers (DONE).
   - Added statics: `pendingTuningReport`, `haveTuningReportA`, `latestTuningReport`,
     `tuningReportPending` (DONE).
   - Added `HandleTuningReportA(frame)` / `HandleTuningReportB(frame)` decoders, placed right after
     `HandleThermalStatus` (DONE). B assembles the full `TuningReport` into `latestTuningReport` and
     sets `tuningReportPending = true` under `TaskCriticalSectionLocker`.
   - **STILL TODO in this file:**
     a. Register `heaterTuningReportA` and `heaterTuningReportB` in the `Spin()` function's frame-type
        switch (currently only handles `pong`/`gpioState`/`thermalStatus`, see around line ~255-270
        pre-edit). Must call `HandleTuningReportA`/`HandleTuningReportB`.
     b. Implement `StartHeaterTuning(bool on, float pwm, float lowTemp, float highTemp, float peakTempDrop)`:
        pack an 8-byte `heaterTuningCommand` payload (byte layout is documented in LpcProtocol.h -
        on:1B, pwm as 0-255 fraction:1B, lowTemp/highTemp as centidegrees int16, peakTempDrop as
        centidegrees uint16) and `Send()` it. Should probably guard on `IsOnline()` like
        `ConfigureHeaterModel` etc. do, EXCEPT the cancel path (on=false) should probably send
        regardless of online state, mirroring how `CommandHeater(..., off, ...)` unconditionally
        sends even when offline (see existing `CommandHeater` for the pattern).
     c. Implement `GetTuningReport(TuningReport& report) noexcept`: under
        `TaskCriticalSectionLocker`, if `!tuningReportPending` return false; else copy
        `latestTuningReport` out, clear `tuningReportPending`, return true. Mirror `GetThermalStatus`'s
        shape but without the online/staleness timeout check (a tuning report has no "staleness"
        concept the way live thermal status does).

## NOT STARTED YET

7. **`RepRapFirmware/src/Heating/LpcHeater.cpp` / `.h`** - the actual consumer. Need to:
   - Check `LpcHeater.h` for what tuning-related private state needs adding (look at what
     `RemoteHeater.h` has: `tOn/tOff/dHigh/dLow/heatingRateAcc/coolingRateAcc/tuningVoltage`
     accumulators are actually declared on the shared `Heater` base class - check
     `RepRapFirmware/src/Heating/Heater.h` around where `RemoteHeater`/`LocalHeater` use them,
     search for `tOn`/`ClearCounters`/`CalculateModel`/`SetAndReportModelAfterTuning` - these are
     likely protected members/methods on `Heater` already, shared across subclasses. CONFIRM THIS
     before assuming LpcHeater needs its own copies.
   - Replace `LpcHeater::StartAutoTune` stub (currently returns
     "Auto tuning is not supported by the LPC heater yet"): validate preconditions similar to
     `SwitchOn()` (online, thermistor sensor assigned, monitors valid), call
     `LpcInterface::StartHeaterTuning(true, tuningPwm, tuningTargetTemp - tuningHysteresis,
     tuningTargetTemp, Heater::TuningPeakTempDrop)`, set an internal "we are tuning" flag (LpcHeater
     doesn't have a CAN-style `TuningState` enum yet - add one, small, similar to RemoteHeater's
     private `TuningState`).
   - Add polling in `LpcHeater::Spin()`: call `LpcInterface::GetTuningReport()` each tick; on a new
     report, feed `tOn/tOff/dHigh/dLow/heatingRateAcc/coolingRateAcc` the same way
     `RemoteHeater::UpdateHeaterTuning` does (see RemoteHeater.cpp ~line 486-501), track
     `tuningCyclesDone`, and once enough cycles are in (mirror `MinTuningHeaterCycles`/
     `MaxTuningHeaterCycles`/`isConsistent` gating from `RemoteHeater::Spin()`'s `TuningState::cycling`
     case, OR simplify since LPC firmware already caps at `MaxTuningCycles=5` fixed cycles - decide
     whether host-side still needs its own consistency check or can just trust "5 cycles done" as
     the finish signal). Call `CalculateModel(...)` then `SetAndReportModelAfterTuning(true)` then
     stop tuning (send `StartHeaterTuning(false, ...)` implicitly not needed since firmware
     self-stops after `MaxTuningCycles`, but a host-initiated cancel path is still needed for
     GCode abort, M0, M112 etc - check how those reach heaters currently, e.g. via `SwitchOff()`).
   - Need a `GetMode()` override returning a tuning-mode `HeaterMode` while tuning is active, mirroring
     `RemoteHeater::GetMode()`.
   - `LpcHeater::Spin()` currently early-parts of it read `LpcInterface::GetThermalStatus` and set
     `mode` from wire state - since the LPC firmware now reports `state=heating` throughout tuning at
     the wire level (see design decision above), `LpcHeater::Spin()` needs to NOT stomp its own
     tuning-tracking mode with the raw wire state while a host-side tuning flag is set. Check this
     interaction carefully - likely need `if (tuning) { pollReport(); } else { existing Spin logic }`.

8. **`tools/protocol_selftest.py`** - add `RoundTrip()` calls for `heaterTuningCommand` (8 bytes),
   `heaterTuningReportA` (14 bytes), `heaterTuningReportB` (16 bytes). Follow existing pattern in the
   file (search for `RoundTrip(MessageType::thermalStatus` for the pattern to copy).

9. **Build + test**:
   - `python3 tools/native_build.py build`
   - `python3 tools/lpc_build.py build` - CHECK FLASH/RAM REPORT against lpc1115.ld budget
     (64 KiB flash / 8 KiB RAM) - this is the issue's explicit "stop and ask" trigger if exceeded.
   - `python3 tools/protocol_selftest.py`
   - Fix any compile errors (haven't compiled anything yet as of this checkpoint).

10. **Commit, push, PR**:
    - Commit with a clear message referencing #19.
    - `git push -u origin lpc-heater-autotune`
    - Open PR via `gh` (or ask - no gh write tool confirmed available yet, check) targeting `main`,
      body mentioning "Closes #19" or "Implements #19" plus a summary of the wire protocol additions
      and the explicit call-out that this hasn't been hardware-tested (issue's own verification
      section requires real-hardware M303 run - flag this clearly as pending/needs manual test).
    - DELETE this file (`WIP_STATUS_M303.md`) before/as part of the PR - it's a scratchpad, not
      meant to be merged.

## Open questions / things to double check while resuming

- Confirm whether `tOn`/`tOff`/`dHigh`/`dLow`/`heatingRateAcc`/`coolingRateAcc`/`tuningVoltage` are
  members of `Heater` base or `RemoteHeater`-only. If base-class, LpcHeater gets them for free.
- Decide the exact stop condition on the host side (trust firmware's fixed 5-cycle cap vs. re-run
  RemoteHeater's consistency check against fewer, host-visible cycles).
- Confirm `LpcHeater::Spin()`'s interaction between wire-reported `state` and a host-side tuning flag
  doesn't cause a flicker/race the moment tuning starts or ends.
