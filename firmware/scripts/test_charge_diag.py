#!/usr/bin/env python3
"""Exercise the production DMA IRQ, recorder and unchanged ADC safety behavior."""
import argparse
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import test_adc_charging as adc
from test_lfp_charger import FW, SHIM, without_timer_init

EXTRA = r'''
#include <string.h>
static void clear_diag(void) {
    memset((void *)&charge_diag, 0, sizeof(charge_diag));
    charge_diag.magic = 0x43484447u; charge_diag.version = 1;
    charge_diag.raw_capacity = CHARGE_DIAG_RAW_COUNT;
    charge_diag.control_capacity = CHARGE_DIAG_CONTROL_COUNT;
}
static void control(uint32_t now, float output, float amps) {
    ChargeDiag_Control(now, 1395, 2, 0, 26.2f, output, 28.4f, amps, amps - 0.2f, 13.0f);
}
int main(void) {
    baseline_main();
    reset(); clear_diag();
    for (unsigned i = 0; i < 40; ++i) adc_inputDmaBuf[i] = (uint16_t)(1000+i);
    test_remaining = 20; test_dma_flags = 2;
    DMA2_Stream0_IRQHandler();
    assert(charge_diag.raw_count == 4 && charge_diag.raw[0].adc[0] == 1000);
    assert(charge_diag.raw[3].adc[4] == 1019 && !adc_diag_tc_pending);
    test_remaining = 40; test_dma_flags = 1;
    DMA2_Stream0_IRQHandler();
    assert(charge_diag.raw_count == 8 && charge_diag.raw[4].adc[0] == 1020);
    assert(adc_diag_tc_pending && !test_dma_flags);
    ADC_input(); assert(!adc_diag_tc_pending && adc_scan_seen);
    test_remaining = 19; test_dma_flags = 3;
    DMA2_Stream0_IRQHandler();
    assert(charge_diag.raw_count == 12 && charge_diag.missed_batches == 1);
    // Crossing into the captured half during the copy rejects the batch.
    test_remaining = 22; test_counter_moves = 1; test_dma_flags = 1;
    DMA2_Stream0_IRQHandler();
    assert(charge_diag.raw_count == 12 && charge_diag.missed_batches == 2);
    test_counter_moves = 0;
    // A full wrap may leave NDTR unchanged and SysTick unable to preempt this
    // IRQ. New completion flags must still reject that overwritten half.
    test_remaining = 40; test_dma_flags = 1; test_completion_on_counter_read = 2;
    DMA2_Stream0_IRQHandler();
    assert(charge_diag.raw_count == 12 && charge_diag.missed_batches == 3);
    assert(test_dma_flags == 3);
    // IRQ must latch errors before acknowledging them (avoid an IRQ storm).
    test_remaining = 20; test_dma_flags = 2 | 16;
    DMA2_Stream0_IRQHandler(); assert(test_dma_flags == 0 && ADC_ChargingFaulted());
    assert(!ADC_ChargingHealthy());
    reset(); clear_diag();
    uint16_t scans[20] = {0};
    for (unsigned i=0; i<300; ++i) {
        scans[0]=(uint16_t)i; ChargeDiag_RawBatch(i, scans);
    }
    assert(charge_diag.raw_count == 1200);
    assert(charge_diag.raw[1196 % 1024].adc[0] == 299);
    assert(!(charge_diag.raw_seq & 1));
    control(1000, 27.0f, 1.8f);
    for (unsigned i=1; i<=140; ++i) control(1000+i*11, 27.0f, 1.8f);
    assert(charge_diag.control_count == 141);
    assert(charge_diag.control[140 % 128].gap_ms == 11);
    assert(!charge_diag.freeze_reason);
    // Unsigned tick wrap is a short valid interval.
    control(UINT32_MAX-5, 27.0f, 1.8f); control(5, 27.0f, 1.8f);
    assert(charge_diag.control[142 % 128].gap_ms == 11);
    control(100, 1.8f, -0.5f); control(349, 1.8f, -0.5f);
    assert(!charge_diag.freeze_reason);
    // A recovered reading cancels the pending failure.
    control(350, 27.0f, 1.8f); control(360, 1.8f, -0.5f);
    control(609, 1.8f, -0.5f); assert(!charge_diag.freeze_reason);
    test_timer.CCR1 = 789;
    control(610, 1.8f, -0.5f);
    assert(charge_diag.freeze_reason == 2 && charge_diag.trigger_tick == 610);
    assert(test_timer.CCR1 == 789); // recorder cannot alter PWM
    uint32_t raw_count = charge_diag.raw_count, controls = charge_diag.control_count;
    ChargeDiag_RawBatch(611, scans); control(620, 27.0f, 1.8f);
    assert(charge_diag.raw_count == raw_count && charge_diag.control_count == controls);
    // Even after freezing, IRQ progress must still feed the original health path.
    adc_diag_tc_pending=0; test_remaining=40; test_dma_flags=1;
    DMA2_Stream0_IRQHandler(); assert(adc_diag_tc_pending);
    FILE *f=fopen("fixture.bin", "wb"); assert(f);
    assert(fwrite((const void *)&charge_diag, sizeof(charge_diag), 1, f)==1); fclose(f);
    clear_diag();
    ChargeDiag_Control(900, 0, 0, 1, 26, 1, 28, -.5f, -.7f, 13);
    assert(charge_diag.freeze_reason == 1);
    // Protection must inspect the ENTIRE DMA half, even after diagnostics freeze.
    reset(); clear_diag(); fresh();
    charge_protection.inhibited=0; charge_protection.starts=1;
    charge_pwm_started=1; test_timer.CCR1=1352;
    adc_inputDmaBuf[3]=0; // first row low; the last row still has good input
    test_remaining=20; test_dma_flags=2;
    DMA2_Stream0_IRQHandler();
    assert(charge_protection.inhibited && test_timer.CCR1==0);
    ChargeDiag_Freeze(test_tick,3);
    charge_protection.inhibited=0; test_timer.CCR1=1352;
    test_remaining=20; test_dma_flags=2;
    DMA2_Stream0_IRQHandler(); assert(charge_protection.inhibited && test_timer.CCR1==0);
    // A slow foreground poll must not fault when IRQ acquisition stayed fresh.
    reset(); fresh();
    for (unsigned i=0; i<5; ++i) {
        test_tick+=8; test_remaining=40; test_dma_flags=1;
        DMA2_Stream0_IRQHandler();
    }
    ADC_input(); assert(ADC_ChargingHealthy() && !ADC_ChargingFaulted());
    // A contact cooldown must leave both rings live for a later real fault.
    reset(); clear_diag();
    for (unsigned i=0; i<30; ++i) fresh();
    charge_protection.restart_pending=1; charge_protection.window_active=1;
    charge_protection.restart_window_ms=test_tick; charge_protection.restarts=3;
    ChargeController();
    assert(charge_protection.cooldown_active && !charge_protection.fault && !charge_diag.freeze_reason);
    raw_count=charge_diag.raw_count; controls=charge_diag.control_count;
    for (unsigned i=0; i<6000; ++i) { fresh(); ChargeController(); }
    assert(!charge_protection.cooldown_active && !charge_protection.inhibited && TIM1->CCR1==0);
    assert(!charge_diag.freeze_reason && charge_diag.raw_count>raw_count && charge_diag.control_count>controls);
    puts("PASS: contact cooldown recovers with live DMA and both diagnostic rings still recording");
    puts("PASS: low/high in one DMA batch cuts PWM, protection survives freeze, fresh IRQ data survives foreground delay");
    puts("PASS: DMA half ordering, late/moving batches, retained error/TC flags, ring wrap, debounce, freeze, tick wrap and PWM unchanged");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    source = (FW / 'src/adc.c').read_text(encoding='utf-8')
    for signature in ['void TIM2_Init(void)', 'void ADC_Charging_Init(void)',
                      'void adc_charging_SetChannel(ADC_Charging_channelSelection_e channel)']:
        source = adc.remove_function(source, signature)
    test = adc.TEST.replace('int main(void)', 'int baseline_main(void)')
    test = test.replace('    adc_charging_fault =', '    adc_diag_tc_pending = 0;\n    adc_charging_fault =')
    with tempfile.TemporaryDirectory(prefix='charge-diag-') as directory:
        out = Path(directory)
        hal = adc.HAL.replace('static uint32_t test_counter(void)',
            'static int test_completion_on_counter_read;\nstatic uint32_t test_counter(void)')
        hal = hal.replace('    if (test_counter_moves)',
            '    if (test_completion_on_counter_read && --test_completion_on_counter_read == 0) test_dma_flags |= 3;\n    if (test_counter_moves)')
        (out / 'main.h').write_text('#pragma once\n' + SHIM + hal +
            '\n#define __get_PRIMASK() 0u\n#define __set_PRIMASK(x) ((void)(x))\n')
        (out / 'adc.h').write_text('#pragma once\nunion FtoU { float f; uint16_t u[2]; };\n')
        for h in ['perimeter.h', 'charger.h']:
            (out / h).write_text('')
        (out / 'adc_under_test.c').write_text(source)
        (out / 'charger_under_test.c').write_text(without_timer_init((FW / 'src/charger.c').read_text()))
        (out / 'diag_under_test.c').write_text((FW / 'src/charge_diag.c').read_text())
        (out / 'test.c').write_text(test + '\n#include "diag_under_test.c"\n' + EXTRA)
        defs = ['CHARGE_DIAGNOSTICS=1', 'BOARD_YARDFORCE500_VARIANT_B=1', 'BOARD_YARDFORCE500B_LFP=1']
        binary = out / ('test.exe' if os.name == 'nt' else 'test')
        if Path(args.cc).stem.lower() == 'cl':
            cmd = [args.cc, '/nologo', '/std:c11', '/utf-8', '/W3', '/I' + str(FW / 'include'),
                   *('/D' + d for d in defs), 'test.c', '/Fe:' + str(binary)]
        else:
            cmd = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                   '-Wno-unused-variable', '-I', str(FW / 'include'), *('-D' + d for d in defs),
                   'test.c', '-lm', '-o', str(binary)]
        subprocess.run(cmd, cwd=out, check=True)
        subprocess.run([str(binary)], cwd=out, check=True)
        from charge_diag_dump import decode
        result = decode((out / 'fixture.bin').read_bytes())
        assert len(result['raw']) == 1024 and len(result['control']) == 128
        assert result['header']['freeze_reason'] == 2
        assert result['raw'][-4]['adc_current'] == 299
        try:
            decode((out / 'fixture.bin').read_bytes()[:-1])
        except ValueError:
            pass
        else:
            raise AssertionError('truncated dump accepted')
        for index, value in [(0, 0), (6, 0), (10, 1), (11, 1)]:
            broken = bytearray((out / 'fixture.bin').read_bytes())
            struct.pack_into('<I', broken, index * 4, value)
            try:
                decode(broken)
            except ValueError:
                pass
            else:
                raise AssertionError('invalid/live/moving dump accepted')
        print('PASS: decoder reads production C layout and rejects truncated/live/moving/invalid dumps')


if __name__ == '__main__':
    main()
