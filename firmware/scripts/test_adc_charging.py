#!/usr/bin/env python3
"""Run production ADC acquisition and charger code with injected HAL faults.

Only peripheral initialization and channel configuration are stubbed. This is
not a timing/electrical model of the STM32; PlatformIO builds cover the real HAL.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from test_lfp_charger import FW, SHIM, without_timer_init


def remove_function(source, signature):
    start = source.index(signature + '\n{')
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[:start] + source[end:]


HAL = r'''
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
#define __DMB() ((void)0)
typedef struct { uint32_t DR; } ADC_TypeDef;
typedef struct { ADC_TypeDef *Instance; } ADC_HandleTypeDef;
typedef struct { uint32_t CR; } DMA_Stream_TypeDef;
typedef struct { DMA_Stream_TypeDef *Instance; } DMA_HandleTypeDef;
typedef int RTC_HandleTypeDef;
static uint32_t test_adc_flags, test_dma_flags, test_remaining;
static int test_start_result, test_counter_moves;
#define HAL_OK 0
#define HAL_ADC_Start_IT(handle) ((void)(handle), test_start_result)
#define ADC_FLAG_OVR 1u
#define DMA_SxCR_EN 1u
#define __HAL_ADC_GET_FLAG(handle, flag) (test_adc_flags & (flag))
#define __HAL_DMA_GET_TC_FLAG_INDEX(handle) 1u
#define __HAL_DMA_GET_HT_FLAG_INDEX(handle) 2u
#define __HAL_DMA_GET_TE_FLAG_INDEX(handle) 4u
#define __HAL_DMA_GET_DME_FLAG_INDEX(handle) 8u
#define __HAL_DMA_GET_FE_FLAG_INDEX(handle) 16u
#define __HAL_DMA_GET_FLAG(handle, flag) (test_dma_flags & (flag))
#define __HAL_DMA_CLEAR_FLAG(handle, flag) (test_dma_flags &= ~(flag))
static uint32_t test_counter(void) {
    if (test_counter_moves) --test_remaining;
    return test_remaining;
}
#define __HAL_DMA_GET_COUNTER(handle) test_counter()
'''

TEST = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "adc_under_test.c"
#include "charger_under_test.c"

void adc_charging_SetChannel(ADC_Charging_channelSelection_e channel) { (void)channel; }
static ADC_TypeDef test_adc;
#if ADC_CHARGING_USES_DMA
static DMA_Stream_TypeDef test_dma;
#endif
static void near(float a, float b) { assert(fabsf(a - b) < 0.0001f); }
static float amps(unsigned raw) { return (raw / 4095.0f * 3.3f - 2.5f) * 100.0f / 12.0f; }
static void reset(void) {
    adc_charging_fault = adc_scan_seen = adc_input_ready = 0;
#if BOARD_YARDFORCE500B_LFP
    charge_protection = (charge_protection_t){ .version=2, .inhibited=1 };
    charge_pwm_started = 1;
#if ADC_CHARGING_USES_DMA
    adc_diag_tc_pending = 0;
#endif
#endif
    adc_last_scan_ms = adc_last_input_ms = test_tick = 0;
    test_adc_flags = test_dma_flags = test_start_result = test_counter_moves = 0;
    ADC_Charging_Handle.Instance = &test_adc;
    adc_charging_eChannelSelection = ADC_CHARGING_CHANNEL_CURRENT;
    charge_current_offset.f = -0.2f;
    ampere_acc.f = 1.0f;
    charger_state = CHARGER_STATE_IDLE;
    chargecontrol_pwm_val = 0;
    battery_voltage = charge_voltage = current = current_without_offset = 0;
#if ADC_CHARGING_USES_DMA
    hdma_adc1.Instance = &test_dma;
    test_dma.CR = DMA_SxCR_EN;
    test_remaining = ADC_DMA_OVERSAMPLE * ADC_CHARGING_CHANNEL_MAX;
    for (unsigned row = 0; row < ADC_DMA_OVERSAMPLE; ++row) {
        adc_inputDmaBuf[row * 5] = 3120;
        adc_inputDmaBuf[row * 5 + 1] = 2200;
        adc_inputDmaBuf[row * 5 + 2] = 3200;
        adc_inputDmaBuf[row * 5 + 3] = 2300;
        adc_inputDmaBuf[row * 5 + 4] = 1200;
    }
#elif BOARD_YARDFORCE500B_LFP
    adc_u32BatteryAcc = adc_u32ChargerAcc = 0;
    adc_u16BatteryCnt = adc_u16ChargerCnt = 0;
#endif
}
static void fresh(void) {
    test_tick += 10;
#if ADC_CHARGING_USES_DMA
    test_dma_flags |= 1u;
#if BOARD_YARDFORCE500B_LFP
    DMA2_Stream0_IRQHandler();
#endif
#else
    const uint16_t scan[] = {3120, 2200, 3200, 2300, 1200};
    for (unsigned i = 0; i < 5; ++i) {
        test_adc.DR = scan[i];
        HAL_ADC_ConvCpltCallback(&ADC_Charging_Handle);
    }
#endif
    ADC_input();
}
static void off(void) {
    ChargeController();
    assert(!ADC_ChargingHealthy());
    assert(!chargecontrol_pwm_val && !TIM1->CCR1 && !chargecontrol_is_charging);
    assert(charger_state == CHARGER_STATE_IDLE);
}
int main(void) {
    reset();
    ADC_input(); off(); // never regulate zero-filled startup buffers
    fresh(); assert(ADC_ChargingHealthy());
    near(current, amps(3120) + 0.2f); // Pi/electronics compensation preserved
    assert(battery_voltage > 26.0f); // filters seeded from actual readings
    // Equal values and equal NDTR across polls do not imply a stall.
    for (unsigned i = 0; i < 100; ++i) { fresh(); assert(ADC_ChargingHealthy()); }
    charger_state = CHARGER_STATE_CHARGING_CC;
    chargecontrol_pwm_val = 500;
    for (unsigned i = 0; i < 4; ++i) {
        test_tick += 10;
        ADC_input(); ChargeController();
    }
    off();
    float saved = ampere_acc.f;
    fresh(); off(); near(ampere_acc.f, saved); // fresh data cannot clear a latched fault

    reset(); fresh();
    HAL_ADC_ErrorCallback(&ADC_Charging_Handle); off();
#if BOARD_YARDFORCE500_VARIANT_B
    reset(); fresh(); test_adc_flags = ADC_FLAG_OVR; off();
#endif
#if ADC_CHARGING_USES_DMA
    for (unsigned flag = 4; flag <= 16; flag *= 2) {
        reset(); fresh(); test_dma_flags = flag; off();
    }
    reset(); fresh(); test_dma.CR = 0; off();
    // A moving snapshot is discarded; it cannot renew input freshness forever.
    reset(); fresh(); test_counter_moves = 1;
    for (unsigned i = 0; i < 4; ++i) { test_remaining = 40; fresh(); }
    off();
#if BOARD_YARDFORCE500B_LFP
    // Row 2 just completed; row 3 is partially overwritten. Never read row 0
    // for current/dock, nor include row 3 in the voltage average.
    reset();
    adc_inputDmaBuf[2 * 5] = 3520;
    adc_inputDmaBuf[0 * 5 + 3] = 0;
    adc_inputDmaBuf[3 * 5 + 1] = adc_inputDmaBuf[3 * 5 + 2] = 0;
    test_remaining = 40 - 3 * 5 - 2;
    fresh();
    near(current, amps(3520) + 0.2f);
    assert(chargerInputVoltage > 29.0f);
    near(battery_voltage, 3200 / 4095.0f * 3.3f * 10.09f + 0.6f);
    near(charge_voltage, 2200 / 4095.0f * 3.3f * 16.0f);
    // At reload (NDTR=N) and terminal count (NDTR=0), row N-1 is latest.
    for (unsigned remaining = 0; remaining <= 40; remaining += 40) {
        reset(); test_remaining = remaining;
        adc_inputDmaBuf[7 * 5] = 3520;
        fresh(); near(current, amps(3520) + 0.2f);
    }
#else
    reset(); test_remaining = 3; fresh(); // row 1 partial, use complete row 0
    assert(ADC_ChargingHealthy());
    near(current, amps(3120) + 0.2f);
#endif
#else
    // Per-conversion rearming failure also latches the charger off.
    reset(); fresh(); test_start_result = 1; fresh(); off();
    // Only a full scan renews freshness, not an interrupt for one channel.
    reset(); fresh(); test_tick += 31;
    adc_charging_eChannelSelection = ADC_CHARGING_CHANNEL_CURRENT;
    HAL_ADC_ConvCpltCallback(&ADC_Charging_Handle); off();
#endif
    // Unsigned timeout remains correct at the HAL tick rollover.
    reset(); test_tick = UINT32_MAX - 20; fresh();
    test_tick += 20; ADC_input(); assert(ADC_ChargingHealthy());
    test_tick += 11; ADC_input(); off();
    puts("PASS: production ADC startup, freshness, faults, snapshot and charger cutoff");
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
        source = remove_function(source, signature)
    with tempfile.TemporaryDirectory(prefix='adc-charging-') as directory:
        out = Path(directory)
        (out / 'main.h').write_text('#pragma once\n' + SHIM + HAL, encoding='utf-8')
        (out / 'adc.h').write_text('#pragma once\nunion FtoU { float f; uint16_t u[2]; };\n', encoding='utf-8')
        for header in ['perimeter.h', 'charger.h']:
            (out / header).write_text('', encoding='utf-8')
        (out / 'adc_under_test.c').write_text(source, encoding='utf-8')
        (out / 'charger_under_test.c').write_text(without_timer_init(
            (FW / 'src/charger.c').read_text(encoding='utf-8')), encoding='utf-8')
        (out / 'test.c').write_text(TEST, encoding='utf-8')
        binary = out / ('test.exe' if os.name == 'nt' else 'test')
        for defs in [['BOARD_YARDFORCE500_VARIANT_B=1', 'BOARD_YARDFORCE500B_LFP=1'],
                     ['BOARD_YARDFORCE500_VARIANT_B=1'], ['BOARD_YARDFORCE500_VARIANT_ORIG=1']]:
            if Path(args.cc).stem.lower() == 'cl':
                cmd = [args.cc, '/nologo', '/std:c11', '/utf-8', '/W3', '/I' + str(FW / 'include'),
                       *('/D' + d for d in defs), 'test.c', '/Fe:' + str(binary)]
            else:
                cmd = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-variable', '-Wno-unused-function', '-I', str(FW / 'include'),
                       *('-D' + d for d in defs), 'test.c', '-lm', '-o', str(binary)]
            subprocess.run(cmd, cwd=out, check=True)
            subprocess.run([str(binary)], cwd=out, check=True)


if __name__ == '__main__':
    main()
