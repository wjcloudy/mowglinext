# .118 motor-temperature discrepancy: ROS1 versus current LFP

Investigated 6 September 2026. Report: about 13 C idle when ambient should be
about 17 C, then a rapid rise toward 50 C when the motor starts.

## Finding

There is a confirmed GPIO initialization defect in both current LFP branches
and the installed diagnostic firmware. The ADC samples channel 13, which is
**PC3**, but initialization selects **PC2** for analogue mode. It also omits
enabling GPIOC's clock before that initialization. On the 500B startup path,
the clock is enabled later by `TF4_Init()`, after `ADC_Charging_Init()`.

The older local ROS1 checkout contains uncommitted changes that fix both
issues. These changes were absent from the imported firmware and subsequent
LFP branches. This is a concrete difference from that ROS1 work, and a strong
candidate contributor to the discrepancy. It is **not yet proven** that this
alone explains the motor-on temperature step or the entire idle offset.

ST's [STM32F401xB/xC datasheet, Table 8, page 38](https://www.st.com/resource/en/datasheet/stm32f401vc.pdf)
maps PC2 to ADC1_IN12 and PC3 to ADC1_IN13. The current `// PC2` comment beside
`ADC_CHANNEL_13` is wrong. Earlier investigation notes repeating that comment
should be read with this correction: this firmware samples PC3, not PC2.

## Source comparison

ROS1 source inspected, without modifying it:

```text
C:/Users/Primary/Documents/GitHub/Mowgli500B-Nekraus/Mowgli/stm32/ros_usbnode/src/adc.c
```

Its checkout is `yardforce-500b-FeaturePanel` at `50f31f6`, with existing local
changes. `git diff -- stm32/ros_usbnode/src/adc.c` shows the GPIO/NTC work below
is uncommitted, so citing HEAD alone does not identify this code. The exact
historical binary used on .118 has not been conclusively matched to this
working tree. Preserve those local edits; do not reset or clean that repository.

| Detail | Local modified ROS1 code | Current IRQ, DMA and diagnostic LFP |
| --- | --- | --- |
| GPIOC clock in ADC initialization | Enabled explicitly for 500B | Missing |
| GPIOC analogue pins | PC2 and PC3 | PC2 only |
| Blade NTC ADC channel | 13, correctly labelled PC3 | 13, incorrectly labelled PC2 |
| Battery NTC | Separate channel 12 / PC2 acquisition | Absent |
| Temperature conversion | 10 kOhm at 25 C, beta 3380 | Same |
| NTC voltage filter | 0.5 new + 0.5 previous | Same; newer code seeds first measurement |
| 500B ADC acquisition time | 480 cycles | 480 cycles |

The relevant minimal correction is to enable GPIOC before `HAL_GPIO_Init`,
configure PC3 as analogue/no-pull for the existing blade channel, and correct
the pin comments. This does not require adding battery-NTC acquisition, changing
the channel order, adding an offset, or changing charge-current calibration.
It belongs in both LFP acquisition variants and the diagnostic variant. The
upstream imported source also has the same defect (`e30092de` import lineage).

## Live evidence, without a reset or motor command

Read-only OpenOCD `mem_ap` capture on .118 at approximately 10:58 UTC:

```text
/home/pi/mower-backups/192.168.1.118/diagnostics/temperature-idle-20260906T105745Z/
```

The Pi watcher was briefly paused to avoid simultaneous ST-Link access and
resumed in a finally block. The MCU was never halted, reset or written. The
directory contains the read configuration, log and binary register/RAM reads.

- GPIOC MODER: `0x0000a400`. PC2 mode bits [5:4] and PC3 bits [7:6] are both
  `00` (digital input), rather than `11` (analogue).
- GPIOC PUPDR: `0x00060002`; PC2/PC3 have no pull selected.
- ADC SQR1: `0x00400000`, SQR3: `0x00d38c41`: five ranks `[1, 2, 3, 7, 13]`.
- Eight NTC column reads: 2061, 2055, 2059, 2056, 2054, 2034, 2060, 2048.
- Filtered NTC voltage: about 1.627 V; temperature: about 12.72 C.
- Separate ROS status sample: 12.35 C, blade RPM 0, firmware 1.9.113/protocol 6.
- External trace and ITM enable registers were both zero.
- Watcher subsequently advancing, reason 0, missed batches 0, maximum gap 11 ms.

These reads were sequential while acquisition ran, not one coherent frozen
snapshot; do not expect the sampled float to exactly match the later DMA rows.
GPIO register state, however, directly establishes the missing analogue setup.

## What the display does, and what remains uncertain

`adc.c` computes `blade_temperature`; `cpp_main.cpp` sends it as
`blade_pkt.temperature`; `hardware_bridge_node.cpp` copies it to
`mower_motor_temperature`. The GUI displays that field directly in Celsius.
It is the blade-motor sensor, not a wheel-motor controller temperature. There
is no newly introduced Celsius conversion or display offset on this path.

The DMA software indexing check already passed all buffer positions: the
latest complete scan's NTC column is selected. That check does **not** test
physical GPIO mode or analogue signal integrity. Sampling phase/cadence does
change between interrupt and DMA operation; the long 480-cycle acquisition
time and temperature conversion did not change.

Under the existing formula, approximately 13 C corresponds to 1.609 V / raw
1996, 17 C to 1.367 V / raw 1696, and 50 C to 0.416 V / raw 516. The large hot
reading therefore requires a substantial measured voltage decrease; it is not
roundoff. The 0.5 voltage filter can follow an electrical step within tens of
milliseconds. A rapid displayed rise does not by itself establish physical
motor heating. Analogue mode is the correct required setup, but input-mode
operation alone does not prove the magnitude or direction of this error.

## Next validation

1. Preserve the active charging investigation and any frozen evidence before
   firmware changes. No temperature patch or reflash was performed in this pass.
2. Make the small GPIO clock/PC3 fix in an isolated change; test initialization
   using a GPIO/clock-aware harness, because the existing conversion tests would
   miss it. Build the relevant 500B targets as a normal user.
3. After a controlled deployment, read back PC3 MODER=`11`, no pull, and confirm
   the channel sequence remains unchanged. Compare a settled cold reading with
   an independent thermometer; do not assume ambient equals motor temperature
   immediately after a previous run.
4. In a supervised blades-removed test, separately record wheel-only and
   blade-motor startup, raw NTC, reported temperature, current, voltage and RPM.
   Check whether the immediate step disappears and whether it reverses on stop.
5. If it persists, measure PC3 and sensor excitation relative to analogue ground
   with the motor off/on; check connector/ground/reference disturbance, sensor
   circuit topology and thermistor constants before changing the conversion.
   A scope can distinguish switching interference from actual temperature change.

This establishes a lost ROS1 GPIO fix; it does not yet establish a complete
explanation of the reported motor-on behavior or a calibrated temperature scale.
