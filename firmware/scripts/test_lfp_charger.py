#!/usr/bin/env python3
"""Compile and run the real charger state machine against a small HAL unit-test shim.

Run: python3 firmware/scripts/test_lfp_charger.py [--cc cc|clang|cl]
MSVC requires a developer command prompt. This does not simulate electrical
dynamics or replace a supervised hardware charge/disconnect test.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / 'firmware/stm32/ros_usbnode'


def without_timer_init(source):
    # Only the hardware timer initialisation is excluded. Compile the production
    # state machine, runtime clamps and board profile verbatim.
    start = source.index(' void TIM1_Init(void)')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[:start] + source[end:]


SHIM = r'''
#include <stdint.h>
typedef int TIM_HandleTypeDef;
static struct { uint16_t CCR1; } test_timer;
#define TIM1 (&test_timer)
static uint32_t test_tick;
#define HAL_GetTick() test_tick
#define HAL_PWR_EnableBkUpAccess() ((void)0)
#define HAL_PWR_DisableBkUpAccess() ((void)0)
#define HAL_RTCEx_BKUPWrite(...) ((void)0)
#define HAL_GPIO_WritePin(...) ((void)0)
'''

ADC = r'''
union FtoU { float f; uint16_t u[2]; };
static union FtoU ampere_acc, charge_current_offset;
static float battery_voltage, charge_voltage, chargerInputVoltage;
static float current, current_without_offset;
static uint8_t test_adc_healthy = 1;
static uint8_t ADC_ChargingHealthy(void) { return test_adc_healthy; }
'''

TEST = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "charger_under_test.c"

static void tick(void) { test_tick += 10; ChargeController(); }
static void ticks(unsigned n) { while (n--) tick(); }
static void near(float a, float b) { assert(fabsf(a - b) < 0.0001f); }

static void dock(void) {
    charger_set_charge_limits(28.5f, 1.8f);
    charger_set_end_voltage(28.5f);
    battery_voltage = 27.0f;
    charge_voltage = 28.0f;
    current = 1.8f;
    chargerInputVoltage = 0;
    ticks(20);
    assert(charger_state == CHARGER_STATE_IDLE);
    assert(chargecontrol_pwm_val == 0);
    chargerInputVoltage = 29.0f;
    tick();
    assert(charger_state == CHARGER_STATE_CONNECTED);
    ticks(11);
    assert(charger_state == CHARGER_STATE_CHARGING_CC);
    near(charge_current_offset.f, -0.20f);
    chargecontrol_pwm_val = 500;
}

int main(void) {
    near(MAX_CHARGE_CURRENT, 1.8f);
    near(MAX_CHARGE_VOLTAGE, 28.5f);
    near(BAT_CHARGE_CUTOFF_VOLTAGE, 28.5f);
    near(MAX_FLOAT_CV_VOLTAGE, 27.5f);
    near(FLOAT_CV_CURRENT, 0.40f);
    near(TICKS_PER_M, 399.0f);
    assert(OPTION_BUMPER == 1);
    assert(IMU_ONBOARD_INCLINATION_THRESHOLD == 0x2C);
#ifdef I_DONT_NEED_MY_FINGERS
#error "LFP release must retain emergency checking"
#endif

    dock();
    current = 1.6f; tick(); assert(chargecontrol_pwm_val == 501);
    current = 1.8f; tick(); assert(chargecontrol_pwm_val == 501);
    current = 1.95f; tick(); assert(chargecontrol_pwm_val == 499);
    current = 2.1f; tick(); assert(chargecontrol_pwm_val == 493);
    current = 2.5f; tick(); assert(chargecontrol_pwm_val == 477);
    chargecontrol_pwm_val = 40; tick(); assert(chargecontrol_pwm_val == 39);
    chargecontrol_pwm_val = MAX_PWM_VALUE; current = 1.6f;
    tick(); assert(chargecontrol_pwm_val == MAX_PWM_VALUE);

    dock();
    battery_voltage = 28.5f;
    ticks(49); assert(charger_state == CHARGER_STATE_CHARGING_CC);
    battery_voltage = 28.4f; tick(); // transient must restart the debounce
    battery_voltage = 28.5f;
    ticks(49); assert(charger_state == CHARGER_STATE_CHARGING_CC);
    tick(); assert(charger_state == CHARGER_STATE_CHARGING_CV);
    assert(chargecontrol_pwm_val == 500); // preserve duty through CC -> CV
    battery_voltage = 27.5f; current = 0.3f;
    ticks(100); assert(chargecontrol_pwm_val == 500); // no float hunting
    current = 0.5f; tick(); assert(chargecontrol_pwm_val == 499);
    battery_voltage = 26.4f; tick();
    assert(charger_state == CHARGER_STATE_CHARGING_CC);
    assert(chargecontrol_pwm_val == 499); // preserve duty on CV -> CC too

    dock();
    chargerInputVoltage = 0;
    ticks(19); assert(charger_state == CHARGER_STATE_CHARGING_CC);
    chargerInputVoltage = 29; tick(); // a short input sag recovers
    chargerInputVoltage = 0;
    ticks(19); assert(charger_state == CHARGER_STATE_CHARGING_CC);
    tick(); assert(charger_state == CHARGER_STATE_IDLE);
    assert(chargecontrol_pwm_val == 0 && TIM1->CCR1 == 0);

    dock();
    charger_set_charge_limits(27.0f, 0.2f);
    battery_voltage = 27.1f; current = 0.3f;
    tick(); assert(chargecontrol_pwm_val < 500); // lower voltage takes effect in CC
    ticks(49); assert(charger_state == CHARGER_STATE_CHARGING_CV);
    unsigned previous = chargecontrol_pwm_val;
    battery_voltage = 27.0f;
    tick(); assert(chargecontrol_pwm_val < previous); // float also obeys 0.2A cap
    charger_set_charge_limits(40.0f, 9.0f);
    near(g_max_charge_voltage, 28.5f);
    near(g_max_charge_current, 1.8f);
    charger_set_charge_limits(0.0f, 0.0f);
    near(g_max_charge_voltage, 28.5f);
    near(g_max_charge_current, 1.8f);
    charger_set_charge_limits(1.0f, 0.01f);
    near(g_max_charge_voltage, 24.0f);
    near(g_max_charge_current, 0.1f);
    dock();
    float saved_ah = ampere_acc.f;
    test_adc_healthy = 0;
    ticks(100);
    assert(charger_state == CHARGER_STATE_IDLE);
    assert(chargecontrol_pwm_val == 0 && TIM1->CCR1 == 0);
    assert(chargecontrol_is_charging == 0);
    near(ampere_acc.f, saved_ah);
    puts("PASS: invalid ADC forces PWM off and freezes charge counting");
    puts("PASS: LFP profile, CC backoff, CV debounce/float, undock, runtime limits");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='lfp-charger-') as directory:
        out = Path(directory)
        (out / 'main.h').write_text(SHIM, encoding='utf-8')
        (out / 'adc.h').write_text(ADC, encoding='utf-8')
        (out / 'charger.h').write_text('', encoding='utf-8')
        (out / 'charger_under_test.c').write_text(
            without_timer_init((FW / 'src/charger.c').read_text(encoding='utf-8')),
            encoding='utf-8')
        (out / 'test.c').write_text(TEST, encoding='utf-8')
        binary = out / ('test.exe' if os.name == 'nt' else 'test')
        def run(source, defs):
            if Path(args.cc).stem.lower() == 'cl':
                command = [args.cc, '/nologo', '/std:c11', '/utf-8', '/W3',
                           '/I' + str(FW / 'include'), *('/D' + d for d in defs),
                           source, '/Fe:' + str(binary)]
            else:
                command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                           '-Wno-unused-variable', '-I', str(FW / 'include'),
                           *('-D' + d for d in defs), source, '-lm', '-o', str(binary)]
            subprocess.run(command, cwd=out, check=True)
            subprocess.run([str(binary)], cwd=out, check=True)

        defs = ['BOARD_YARDFORCE500_VARIANT_B=1', 'BOARD_YARDFORCE500B_LFP=1']
        run('test.c', defs)
        # The generic GUI form must not override the LFP chemistry's compiled
        # envelope when its template defines are present ahead of defaults.
        run('test.c', defs + ['MAX_CHARGE_CURRENT=1.2f', 'MAX_CHARGE_VOLTAGE=29.4f',
                             'BAT_CHARGE_CUTOFF_VOLTAGE=29.2f',
                             'IMU_ONBOARD_INCLINATION_THRESHOLD=0x38'])
        (out / 'stock.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include "board.h"
int main(void) {
    assert(MAX_CHARGE_CURRENT == 1.2f && MAX_CHARGE_VOLTAGE == 29.4f);
    assert(BAT_CHARGE_CUTOFF_VOLTAGE == 29.2f);
    assert(IMU_ONBOARD_INCLINATION_THRESHOLD == 0x38 && OPTION_BUMPER == 0);
#ifdef BOARD_YARDFORCE500_VARIANT_B
    assert(TICKS_PER_M == 277.0);
#else
    assert(TICKS_PER_M == 339.0);
#endif
#ifdef FLOAT_CV_CURRENT
#error "LFP constants leaked into a stock profile"
#endif
    puts("PASS: stock board retains upstream defaults");
    return 0;
}
''', encoding='utf-8')
        run('stock.c', ['BOARD_YARDFORCE500_VARIANT_B=1'])
        run('stock.c', ['BOARD_YARDFORCE500_VARIANT_ORIG=1'])


if __name__ == '__main__':
    main()
