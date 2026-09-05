# Custom Yardforce 500B LFP firmware

Updated onto upstream `dev` at `5cb07fabb72f4c1a8a31cc8857eb61814cbe986c`
(5 September 2026). Both branches retain the custom 8S LiFePO4 charging and
500B hardware changes. Protocol v6, firmware build identification, runtime
kinematics and safety limits, yaw control, anti-dig and emergency aborts come
from current upstream.

## Branches and builds

| Branch | Charging ADC | Build |
| --- | --- | --- |
| `fix/wheel-pi-ticks-lfp-adc` | Circular DMA; average 8 battery/charge-voltage scans | `pio run -e Yardforce500B_LFP` |
| `fix/wheel-pi-ticks-lfp` | Per-conversion interrupt; average samples collected in each 10 ms window | `pio run -e Yardforce500B_LFP` |

The `-lfp-adc` branch is the newest reference (`185f1306` before this merge).
Its DMA acquisition also applies to the plain 500B target, without LFP
oversampling. The interrupt branch additionally retains
`Yardforce500B_LFP_DEBUG`, with `-Og -g3` and the original remote debug endpoint.
Both branches otherwise share the charging logic and custom hardware features.
The LFP environment inherits the upstream 500B toolchain and build-id hook.
Select it explicitly: the default `pio run` target remains the stock 500.

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
| Fixed current offset | -0.20 A |
| Low / critical battery voltage | 24.0 / 23.0 V |
| Dock input threshold / disconnect debounce | 22.0 V / 20 cycles (~200 ms) |
| CC-to-CV debounce / return hysteresis | 50 cycles (~500 ms) / 2.0 V |
| CV deadband | +/-0.2 V |
| PWM floor / ceiling | 39 / 1395 (timer period 1400) |
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
  from wheel-lift emergency reporting. Bump debounce is 100 ms while mowing,
  500 ms while docking, followed by a one-second reverse. The event is still
  firmware-local and is not reported to Nav2. Current upstream emergency aborts
  during reverse and settle are retained.
- Onboard LIS3DH tilt threshold remains 0x2C with the 500 ms trip timeout.
  Failed I2C reads retain the custom behavior of returning **no tilt**. This
  suppresses the reported phantom dock trips but is not detection of a failed
  tilt sensor; a disconnected sensor still needs separate diagnosis.
- PB3 trace ownership is cleared before soft-I2C startup; bounded SWO output
  prevents an undrained trace FIFO hanging boot. The 500B USB D+ disconnect
  pulse remains, allowing re-enumeration after reset.
- Preserve the documented **hard power cycle after flashing**. PB3 is both
  SWO and the J18 MPU6050 clock; trace activity during flashing can disturb the
  IMU even when firmware later reclaims the pin. The no-TPIU command remains
  documented beside the LFP environment in `platformio.ini`.
- Upstream's normal emergency-enabled release default is retained. The old
  unconditional bench `I_DONT_NEED_MY_FINGERS` define is not carried forward.

## Verification and limits

Run as the normal project user:

```sh
python3 firmware/scripts/board_defaults_parity.py
python3 firmware/scripts/protocol_version_guard.py --check
python3 firmware/scripts/sync_ros_lib.py --check
python3 firmware/scripts/test_lfp_charger.py
```

The charger unit harness compiles the production controller with a minimal HAL
shim, excluding only timer initialization. It exercises bulk ramp/backoff,
CV entry/debounce, float stability, fallback, disconnect debounce, fixed offset,
runtime ceilings, and isolation from stock/GUI defaults. It runs with GCC/Clang
or `--cc cl` in a Windows MSVC developer prompt. Use Python `-X utf8` on Windows
for the existing source-generation guards. CI builds the LFP environment too.

Builds and unit tests cannot establish electrical stability. Before deployment,
remove blades and supervise measurement of voltage/current against a meter,
bulk-to-float operation, runtime limit reduction, disconnect/reconnect, DMA/ADC
freshness, bumper response, emergency during reverse, and IMU boot after flash.
The June history reports hardware tests for charging fixes, but does not establish
that this September combination has been tested on the mower.

Existing SOC limitations remain: the charge accumulator subtracts the offset
again after ADC correction, and firmware still transmits battery percentage as
zero. The ROS battery gauge remains voltage-derived, not an LFP coulomb-counting
gauge. This merge preserves the charging work without claiming those are solved.
