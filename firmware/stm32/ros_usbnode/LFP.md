# Custom Yardforce 500B LFP firmware

Maintain **`codex/lfp-firmware`** in `wjcloudy/mowglinext` from now on. It combines
the three former firmware branches, including upstream dev `ea634cd9`, protocol
7, LFP charging, sensor recovery, temperature correction and blade reversal.
Acquisition and monitoring are independent compile-time choices.

## Build targets

| Acquisition | Charge monitoring | Build environment |
| --- | --- | --- |
| Interrupt; average each foreground window | Off | `Yardforce500B_LFP_IRQ` |
| Circular DMA; average 8 voltage scans | Off | `Yardforce500B_LFP_DMA` |
| Interrupt | On | `Yardforce500B_LFP_IRQ_DIAG` |
| Circular DMA | On | `Yardforce500B_LFP_DMA_DIAG` |

From `firmware/stm32/ros_usbnode`, use `pio run -e <environment>`. The default
remains stock 500. `Yardforce500B_LFP` is the base IRQ target with monitoring off;
`Yardforce500B_LFP_DIAG` remains a compatibility alias for **DMA + monitoring**.
`Yardforce500B_LFP_DEBUG` retains the old IRQ remote-debug configuration.
All LFP targets retain PWM ceiling 1390. Monitoring uses recorder ABI 2.
Use its matching ELF when obtaining recorder addresses; never reuse an address
from another build. Plain LFP builds omit the diagnostic recorder.

`include/firmware_features.h` defaults both `ADC_CHARGING_DMA` and
`CHARGE_DIAGNOSTICS` to 0. Each must be 0 or 1. DMA is supported on 500B only;
monitoring requires 500B LFP, with either sampler. Standard 500/500B targets
select IRQ and no monitoring. Neither flag selects battery chemistry or changes
charge limits: those remain selected by `BOARD_YARDFORCE500B_LFP`.

### Former branches (frozen reference points)

| Former branch / commit | Replacement target |
| --- | --- |
| `fix/wheel-pi-ticks-lfp` / `79eae808` | `Yardforce500B_LFP_IRQ` |
| `fix/wheel-pi-ticks-lfp-adc` / `01517f87` | `Yardforce500B_LFP_DMA` |
| `codex/lfp-charge-early-capture` / `629ae37f` | `Yardforce500B_LFP_DMA_DIAG` |

Keep these old branches as deployment history; merge future upstream dev work
only into `codex/lfp-firmware`. No deployment is implied by this consolidation:
.118 was last flashed from `629ae37f`, firmware 1.11.92/protocol 7. The new IRQ
monitoring combination has software coverage but no physical qualification yet.

### Upstream maintenance

Keep both ADC paths in shared `adc.c`; select DMA hardware setup, timer period,
scan ranks, IRQ handling and averaging with `ADC_CHARGING_DMA`, not board type.
The actual MCU still selects HAL constants. Keep charge protection active whether
monitoring is disabled, live or frozen. Run the native harnesses and CI build
matrix (four LFP combinations plus stock boards and retained debug target).
Do not publish LFP binaries under stock release manifest entries; release packaging
and GUI build selection can be proposed separately upstream.

HARDWARE_PENDING for the consolidated targets: use .118/500B LFP, record the exact
commit, binary SHA256, target/flags and host digest; use a matching protocol 7 host.
With blades removed, clear rotor/wheels and accessible cutoff, verify IDLE/zero
motion, progressing ADC/IMU data, healthy tilt, correct temperature and 28.5 V/1.8 A
charge ceilings. Supervised redocking must cut duty on input loss and restart only
on fresh stable input. For monitoring builds, require a frozen dump with the correct
sample provenance and no loss of charging protection. Overnight charging and blade
reversal require separate field acceptance; earlier images' measurements do not
qualify these newly built targets.

## Blade reversal synchronization

The production driver and regression harness come from PR #559, including
continued receive/error handling while TX is busy and release only on a newly
received qualifying ESC reply. Reversal keeps OFF for at least 1 second and
requires inactive, error-free zero reports spanning at least 300 ms; stale or
invalid replies cannot release it. Pending reversal has no forced timeout.

The ESC speed word is not a verified live coast-down measurement. The observed
500 holds its last value after OFF before clearing it. See
[BLADE-REVERSE.md](BLADE-REVERSE.md) for the exact 500 test baseline; this does not
establish physical stop timing on the 500B or validate the new LFP combination.
The native blade harness also compiles with the real LFP board selection.
500B reversal timing remains HARDWARE_REQUIRED: use the exact deployed commit,
ELF/binary hashes and ESC revision, with blades removed, wheels raised, a clear
rotor and an accessible cutoff. Observe forward/OFF/reverse/OFF and confirm the
rotor physically stops before opposite rotation; retain USB telemetry alongside
that observation. A firmware flash alone does not satisfy this acceptance test.

## Preserved charge profile

The profile is now single-sourced in `include/board_defaults.h`, selected by
`BOARD_YARDFORCE500B_LFP`. It overrides generic GUI template charge values so
rendering a stock configuration cannot turn the LFP build into a Li-ion profile.

| Setting | Value |
| --- | --- |
| Pack capacity for existing charge counting | 4.8 Ah |
| Bulk current / maximum charge voltage | 1.8 A / 28.5 V |
| Float voltage / current cap | 27.5 V / 0.40 A |
| Full indication current threshold | 0.25 A |
| Fixed Pi/electronics current compensation | -0.20 A |
| Low / critical battery voltage | 24.0 / 23.0 V |
| Dock input threshold / disconnect debounce | 22.0 V / 20 cycles (~200 ms) |
| CC-to-CV debounce / return hysteresis | 50 cycles (~500 ms) / 2.0 V |
| CV deadband | +/-0.2 V |
| PWM floor / ceiling | 39 / 1390 (timer period 1400; 7 September peak-duty trial, formerly 1395) |
| Battery / charge-rail IIR weights | 0.05 / 0.10 |

Bulk PWM rises one count at a time and backs off by 1/2/6/16 counts according
to overcurrent. CV entry uses battery voltage, not the charge rail. The float
target stays at or below the active charge target, and a low battery returns
to CC without resetting PWM. Full indication updates the existing charge
counter; it does not terminate charging (the END transition remains disabled).

## Runtime host settings

Upstream now reapplies charge limits and motor calibration after connection.
Its default current is 1.2 A, so firmware alone no longer guarantees your
previous 1.8 A bulk limit. Merge these values into the existing installed
`mowgli_robot.yaml` under `mowgli.ros__parameters`, then restart the ROS stack:

```yaml
mowgli:
  ros__parameters:
    max_charge_voltage: 28.5
    max_charge_current: 1.8
    ticks_per_meter: 399.0
```

Preserve the rest of the installed file, including site data and tuning.
These are values for this specific pack and drivetrain, not new defaults for
all mowers. Existing calibrated `wheel_pid_*` values continue to be reapplied;
the LFP firmware's pre-connection feedforward fallback remains 300 PWM/(m/s).

Runtime charge limits can lower the compiled envelope. This merge makes the
LFP CC target and float current cap respect those limits too: a 27 V request
constrains CC/CV to that target, and a 0.2 A request also reduces the float cap.
The old `charger_set_end_voltage()` API remains, capped by the active ceiling.
No packet change is needed; the current v6 host and firmware must be paired.

## Custom hardware and startup

- Blue wheel-lift input remains the front bumper; it is intentionally excluded
  from wheel-lift emergency reporting. A bump taken while the charger input is
  already above `MIN_DOCKED_VOLTAGE` is treated as dock contact: the outgoing
  drive message is held at zero for that tick so the charge current can
  establish, instead of reversing back out of the cradle. That guard runs ahead
  of the mode switch, because `OPENMOWER_STATUS_DOCKING` is never assigned --
  the ROS1 `mower_msgs/HighLevelStatus` SUBSTATE byte that used to select it
  was dropped when the ROS2 COBS protocol replaced it (`pkt_hl_state_t` carries
  `current_mode` only), so an auto-dock arrives here as
  `OPENMOWER_STATUS_MOWING` and used to take the 100 ms collision reverse
  straight out of the dock. Off the dock the charger input is ~0 and the
  existing collision behaviour is reached unchanged: 100 ms debounce while
  mowing (500 ms in the still-unreachable docking case), followed by a
  one-second reverse. The guard only overrides that tick's message;
  `drivemotor_eState` stays `DRIVEMOTOR_RUN`, so normal cmd_vel drive resumes
  as soon as the bumper opens or the rail drops. The event is still
  firmware-local and is not reported to Nav2. Current upstream emergency aborts
  during reverse and settle are retained.
- Onboard LIS3DH tilt threshold remains 0x2C with the 500 ms trip timeout.
  Failed or stale onboard sensor reads now inhibit motion and latch a sensor fault.
  The foreground recovery verifies configuration and fresh status; it does not
  clear the emergency. This replaces the former no-tilt-on-read-error policy.
  See [I2C-RECOVERY.md](I2C-RECOVERY.md) for both sensor buses.
- PB3 trace ownership is cleared before soft-I2C startup. PB3 is also the J18
  MPU6050 clock, so keep SWO trace disabled when flashing or attaching a debugger.
  The 500B USB D+ disconnect pulse allows re-enumeration after reset.
- Disable internal ITM output as well as external trace: the bounded SWO wait is
  per character, so a whole debug message can still trigger the watchdog when
  ITM is enabled without a reader. See the flashing procedure below.
- Upstream's normal emergency-enabled release default is retained. The old
  unconditional bench `I_DONT_NEED_MY_FINGERS` define is not carried forward.

## Flashing without a physical power cycle

The .118 STM32F401 mower recovered after flashing on 6 September 2026 without
cycling power. The critical OpenOCD command is **`itm ports off`**, placed after
Cortex-M target creation and before `init`. OpenOCD automatically enables ITM
port 0 when creating that target, even without a TPIU; see the
[OpenOCD ITM documentation](https://openocd.org/doc/html/Architecture-and-Core-Commands.html).
Clearing `DBGMCU_CR` / PB3 `TRACE_IOEN` alone is insufficient: on .118 it was
already zero while ITM remained enabled and debug output caused watchdog boot
loops. Disabling ITM restored normal IMU telemetry and charging without a
physical power cycle. This supersedes the old mandatory hard-boot advice.

Build as the normal project user from `firmware/stm32/ros_usbnode`:

```sh
pio run -e Yardforce500B_LFP
```

Back up the installed flash first and stop the ROS hardware bridge for the
maintenance window. Copy the selected branch's `.pio/build/Yardforce500B_LFP/firmware.bin`
and `remote_upload/yardforce500b_no_trace.cfg` to the Pi connected to ST-Link,
verify the transferred binary's checksum, then run there (adjust both paths):

```sh
sudo openocd -c "set FIRMWARE /absolute/path/firmware.bin" \
  -f /absolute/path/yardforce500b_no_trace.cfg
```

The helper combines the OpenOCD programming/recovery and ITM-off settings
verified on .118: it uses a minimal target without TPIU, 100 kHz SWD, disables
ITM before initialization, checks device ID `0x423` (STM32F401xB/C), writes and
verifies the binary at `0x08000000`, then issues `reset run` using SYSRESETREQ.
An NRST wire is not required. This helper is specific to the 500B STM32F401;
do not use it for the standard 500's STM32F103.

Require successful verification before restarting the bridge, then check live
IMU samples, firmware identification and plausible battery/current telemetry.
Retain the backup and flashing log. Keep ITM disabled on later debugger
attachments too. A physical power cycle is a fallback if peripherals still fail
to recover, not a required flashing step. `st-flash --reset` alone did not solve
this on .118: the installed st-flash 1.7.0 failed during programming, and OpenOCD
was needed to recover. The default PlatformIO upload command is unchanged;
use the explicit helper above for this procedure.


### If the onboard I2C bus remains stuck after reset

On .118 with diagnostics firmware `0df13383` / 1.11.13 on 19 September 2026,
the first flash left I2C1 busy (`SR2=0x0002`, SCL high, SDA low). Ordinary MCU
reset did not clear it. The tilt-sensor read delayed the main loop, IMU output
fell to about 33 Hz, and blade status was starved (temperature displayed zero).
ITM and PB3 trace were already disabled. This is distinct from the trace issue.

With the bridge stopped and blades removed, the standalone recovery helper
resets/halts the MCU (PWM reset), releases PB6/PB7 as open-drain pins, clocks
nine SCL pulses, emits STOP and verifies both lines high before restarting:

```sh
sudo openocd -f /absolute/path/yardforce500b_recover_i2c.cfg
```

It does not flash firmware or bypass a safety input. Failed line checks leave
the CPU halted; investigate the bus instead of blindly resuming. Restart the
bridge only after success and verify fresh blade status, IMU samples and power
telemetry. On that exact .118 build, this restored about 88 Hz IMU output,
22.5 C blade temperature, charging around 1.8 A and the SFTRST reset cause,
without a physical power cycle. The firmware binary SHA256 was
`52b7930bc5b3e66ff52dc633c234f6fa9530ecbc5f34392a44edcedba286679f`.
This validates recovery on that board/build, not blade reversal timing or a
change to the charging profile. Full evidence is retained in the deployment
`2026-09-19_lfp-blade_0df13383` on .118.

## Charging contact-loss protection

The September charging-only overlay stops duty on acquired input loss, requires
250 ms of stable input before a zero-duty restart, limits repeated restarts,
and latches failed-output faults. See [CHARGING-MAINTENANCE.md](CHARGING-MAINTENANCE.md)
for the hooks, constants, branch differences and upstream merge checklist.

## ADC fault handling

All three branches hold charge PWM at zero until ADC input is valid. A start/rearm
failure, ADC error, or more than 30 ms without acquisition progress latches
charging off until reboot. The normal 10 ms controller cadence checks that
deadline; this is not an asynchronous hardware cutoff. The charge counter stops
integrating invalid input. Voltage/current filters start at the first measured
values so the controller never ramps against a zero-filled startup buffer.

On the DMA branch, ADC overrun, DMA transfer/direct-mode/FIFO errors and a
disabled stream also latch the fault. LFP completion/error interrupts provide the contact-loss guard and acquisition
timestamps; stock 500B still polls its completion flags. Current, dock voltage and NTC use the latest
completed row. Stock 500B keeps two rows (without averaging) so a completed
scan remains available during a write; LFP keeps eight. Voltage averaging
excludes a row being overwritten. A snapshot
that moves while copied is discarded, and persistent snapshot failure also
expires input freshness. The IRQ branch tracks completion of all five channels.

The fixed -0.20 A Pi/electronics compensation is retained on all three branches.
Charge-counter accounting is unchanged pending confirmation of whether that
counter should include the electronics' consumption: it currently subtracts
the offset again after the ADC current correction.

## Verification and limits

Run as the normal project user:

```sh
python3 firmware/scripts/board_defaults_parity.py
python3 firmware/scripts/protocol_version_guard.py --check
python3 firmware/scripts/sync_ros_lib.py --check
python3 firmware/scripts/test_blade_reverse.py
python3 firmware/scripts/test_lfp_charger.py
python3 firmware/scripts/test_adc_charging.py
```

The charger unit harness compiles the production controller with a minimal HAL
shim, excluding only timer initialization. It exercises bulk ramp/backoff,
CV entry/debounce, float stability, fallback, disconnect debounce, fixed offset,
runtime ceilings, invalid-input shutdown, and isolation from stock/GUI defaults. It runs with GCC/Clang
or `--cc cl` in a Windows MSVC developer prompt. Use Python `-X utf8` on Windows
for the existing source-generation guards. CI builds the LFP environment too.

The ADC harness compiles production acquisition, error callbacks and charger
code with injected HAL values/flags. It checks startup, constant signals, frozen
acquisition, error latching, tick rollover, DMA ring wrap, partial rows and moving
snapshots for LFP, stock 500B and original 500 configurations.

Builds and unit tests cannot establish electrical stability. Before deployment,
remove blades and supervise measurement of voltage/current against a meter,
bulk-to-float operation, runtime limit reduction, disconnect/reconnect, DMA/ADC
freshness, bumper response, emergency during reverse, and IMU boot after flash.
The June history reports hardware tests for charging fixes, but does not establish
that this September combination has been tested on the mower.

Existing SOC limitations remain: the accounting convention above is unresolved,
and firmware still transmits battery percentage as zero. The ROS battery gauge remains voltage-derived, not an LFP coulomb-counting
gauge. This merge preserves the charging work without claiming those are solved.

## Upstream dev refresh — 2026-09-26

Merged upstream `ea634cd902cedd3b606e66e70a88069db0c4b9f1` into the three LFP
lineages without changing their ADC implementation, charge regulator/protection,
1390 PWM ceiling, -0.20 A offset, PC3 temperature input or optional recorder.
Upstream now uses USB protocol **7** and saves runtime parameters to flash.
Install a matching protocol-7 ROS2 host before returning the mower to service.
The upstream emergency generation/physical-input reset protections are retained.

The LFP overlay in `fw_param_catalog.h` derives its charge envelope from
`board_defaults.h`: 24–28.5 V and 0.1–1.8 A. This protects compiled defaults,
USB SET_PARAM and records loaded from flash, and reports the actual bounds to
the host. Stock 500/500B builds retain upstream's 25.2–29.4 V / 0.1–1.2 A envelope.
`test_fw_params.py` checks stock and LFP profiles, lower-current persistence,
out-of-profile saved values and the reported upper bound. Reapply this overlay
when merging future catalog changes; do not change parameter IDs.

On 500B, sector 5 at `0x08020000` is reserved for parameters. Keep the image below
128 KiB, preserve this sector on later flashes, and include all 256 KiB in a
preflash backup. First v7 boot may erase foreign data in that reserved sector.
Use the documented ITM-off flash procedure; do not enable periodic SWD polling.

HARDWARE_PENDING after software verification: on .118 (500B/8S LFP), record the
exact image SHA256, commit, host image digest and protocol. With blades removed,
stationary mower, clear wheels/rotor and accessible cutoff, verify startup stays
IDLE with zero wheel/blade motion, compatible USB telemetry, advancing IMU and
healthy onboard tilt. Supervise redocking and require zero duty off-dock and
bounded fresh-input restart; verify the 28.5 V / 1.8 A limits. Overnight charging
and physical blade reversal remain separate acceptance runs on that exact build;
no prior hardware measurement proves their result after this merge.
