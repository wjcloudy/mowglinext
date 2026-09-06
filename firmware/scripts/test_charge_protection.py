#!/usr/bin/env python3
"""Production charger regression tests for contact loss, restart and output faults."""
import test_lfp_charger as test

# Inject an input IRQ immediately before the foreground masks interrupts for
# its final PWM commit. The zero written by the IRQ must win this interleaving.
test.SHIM = test.SHIM.replace('#define __disable_irq() ((void)0)',
    'static void test_mask(void);\n#define __disable_irq() test_mask()')
test.TEST = test.TEST[:test.TEST.index('int main(void)')] + r'''
static unsigned interrupt_on_mask;
static void test_mask(void) {
    if (interrupt_on_mask && !--interrupt_on_mask) Charger_InputSample(0, test_tick);
}
static void stable(unsigned ms) {
    chargerInputVoltage = 28.4f;
    for (unsigned i=0; i<ms; ++i) {
        ++test_tick; Charger_InputSample(2203, test_tick);
    }
}
int main(void) {
    dock(); current=1.8f;
    chargecontrol_pwm_val=1352; tick(); assert(TIM1->CCR1==1352);
    Charger_InputSample(0, test_tick);
    assert(TIM1->CCR1==0 && charge_protection.inhibited); // no foreground call needed
    Charger_InputSample(2203, test_tick); // bounce returns within one batch
    ChargeController(); assert(chargecontrol_pwm_val==0);
    stable(249); ChargeController(); assert(TIM1->CCR1==0 && charge_protection.inhibited);
    stable(1); ChargeController();
    assert(!charge_protection.inhibited && charger_state==CHARGER_STATE_CONNECTED && TIM1->CCR1==0);
    ticks(10); assert(TIM1->CCR1==0);
    current=0; tick(); assert(TIM1->CCR1==1); // restart from zero, never 1352
    for (unsigned i=0;i<10;++i) {
        ++test_tick; Charger_InputSample(2203,test_tick); ChargeController();
        assert(TIM1->CCR1==1); // faster caller must not accelerate ramp
    }
    ++test_tick; Charger_InputSample(2203,test_tick); ChargeController(); assert(TIM1->CCR1==2);
    puts("PASS: IRQ cutoff, low/high batch retention, 250 ms qualification, zero restart, time-based ramp");

    dock(); current=1.8f; chargecontrol_pwm_val=1352;
    interrupt_on_mask=2; tick();
    assert(TIM1->CCR1==0 && chargecontrol_pwm_val==0 && !chargecontrol_is_charging);
    puts("PASS: ADC loss racing final PWM commit cannot restore high duty");

    dock(); current=0;
    for (unsigned attempt=0; attempt<4; ++attempt) {
        Charger_InputSample(0,test_tick); ChargeController();
        stable(251); ChargeController();
        if (attempt<3) { assert(!charge_protection.fault); ticks(11); }
    }
    assert(charge_protection.fault==CHARGE_FAULT_RESTART_LIMIT && TIM1->CCR1==0);
    stable(61000); ChargeController(); assert(TIM1->CCR1==0 && charge_protection.fault);
    puts("PASS: three contact-recovery attempts allowed; fourth latches, time alone cannot clear fault");

    dock(); chargecontrol_pwm_val=1352; current=1.8f; tick();
    test_tick+=31; ChargeController(); assert(TIM1->CCR1==0 && charge_protection.inhibited);
    stable(100); Charger_InputSample(1740,test_tick); // hysteresis band breaks qualification
    stable(249); ChargeController(); assert(charge_protection.inhibited);
    stable(1); ChargeController(); assert(charge_protection.inhibited);
    stable(1); ChargeController(); assert(!charge_protection.inhibited);
    puts("PASS: stale input inhibits; hysteresis does not count as stable recovery");

    dock(); test_tick=UINT32_MAX-100;
    Charger_InputSample(0,test_tick); ChargeController();
    stable(251); ChargeController();
    assert(!charge_protection.inhibited && TIM1->CCR1==0);
    puts("PASS: stable-input timer crosses HAL tick wrap");

    dock(); current=3.0f; battery_voltage=charge_voltage=27.5f;
    chargecontrol_pwm_val=1352; charger_state=CHARGER_STATE_CHARGING_CV;
    tick(); assert(TIM1->CCR1==1336);
    chargecontrol_pwm_val=1; tick(); assert(TIM1->CCR1==0);
    charger_state=CHARGER_STATE_CHARGING_CC; battery_voltage=29; tick(); assert(TIM1->CCR1==0);
    puts("PASS: CV proportional current backoff; voltage/current reduction never raises zero to PWM floor");

    dock(); current=-.5f; charge_voltage=1.8f; battery_voltage=26.2f;
    stable(2100); chargecontrol_pwm_val=1352; charger_state=CHARGER_STATE_CHARGING_CC;
    tick(); ticks(22); assert(!charge_protection.fault);
    charge_voltage=27; tick(); assert(!charge_protection.fault); // recovery cancels debounce
    charge_voltage=1.8f; tick(); ticks(23);
    assert(charge_protection.fault==CHARGE_FAULT_OUTPUT && TIM1->CCR1==0 && !chargecontrol_is_charging);
    stable(1000); ChargeController(); assert(TIM1->CCR1==0);
    puts("PASS: sustained requested/no-output failure latches; no automatic hardware-fault retry");

    dock(); current=0; charge_voltage=battery_voltage=27.5f;
    charger_state=CHARGER_STATE_CHARGING_CV; chargecontrol_pwm_val=1300;
    ticks(500); assert(!charge_protection.fault && TIM1->CCR1==1300);
    puts("PASS: full-battery float at zero net current is not a failed-output fault");
    return 0;
}
'''
test.main()
