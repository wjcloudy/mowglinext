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
    assert(!charge_protection.fault && charge_protection.cooldown_active && TIM1->CCR1==0);
    assert(charge_protection.cooldowns==1 && charge_protection.restarts==3);
    stable(59999); ChargeController(); assert(TIM1->CCR1==0 && charge_protection.cooldown_active);
    stable(1); ChargeController();
    assert(!charge_protection.cooldown_active && !charge_protection.inhibited);
    assert(charger_state==CHARGER_STATE_CONNECTED && TIM1->CCR1==0 && charge_protection.restarts==1);
    ticks(10); assert(TIM1->CCR1==0);
    tick(); assert(TIM1->CCR1==1);
    puts("PASS: fourth contact restart pauses 60 seconds, then resumes from zero without reboot");

    // A second burst is bounded too; bounce cannot shorten or extend cooldown.
    for (unsigned attempt=0; attempt<3; ++attempt) {
        Charger_InputSample(0,test_tick); ChargeController();
        stable(251); ChargeController();
        if (attempt<2) ticks(11);
    }
    assert(charge_protection.cooldown_active && charge_protection.cooldowns==2);
    unsigned cooldown_start=charge_protection.cooldown_since;
    stable(59900); Charger_InputSample(0,test_tick); ChargeController();
    assert(charge_protection.cooldown_since==cooldown_start && TIM1->CCR1==0);
    stable(100); ChargeController(); assert(charge_protection.inhibited && TIM1->CCR1==0);
    stable(150); ChargeController(); assert(charge_protection.inhibited);
    stable(1); ChargeController(); assert(!charge_protection.inhibited && TIM1->CCR1==0);
    puts("PASS: repeated bursts remain bounded; expiry still requires 250 ms of fresh stable input");

    // Expiry without fresh samples must not energize the charger.
    dock(); Charger_InputSample(0,test_tick); ChargeController();
    charge_protection.cooldown_active=1; charge_protection.cooldown_since=test_tick;
    test_tick+=60000; ChargeController(); assert(charge_protection.inhibited && TIM1->CCR1==0);
    stable(251); ChargeController(); assert(!charge_protection.inhibited && TIM1->CCR1==0);

    // Unsigned elapsed time must work across the 32-bit millisecond wrap.
    dock(); test_tick=UINT32_MAX-100;
    Charger_InputSample(0,test_tick); ChargeController();
    charge_protection.cooldown_active=1; charge_protection.cooldown_since=test_tick;
    stable(59999); ChargeController(); assert(charge_protection.cooldown_active && TIM1->CCR1==0);
    stable(1); ChargeController(); assert(!charge_protection.inhibited && TIM1->CCR1==0);
    puts("PASS: cooldown expiry rejects stale input and survives tick wrap");

    // Neither genuine fault is cleared by contact-cooldown expiry.
    for (unsigned fault=CHARGE_FAULT_ADC; fault<=CHARGE_FAULT_OUTPUT; ++fault) {
        dock(); Charger_InputSample(0,test_tick); ChargeController();
        charge_protection.cooldown_active=1; charge_protection.cooldown_since=test_tick;
        charge_protection.fault=fault;
        stable(61000); ChargeController();
        assert(charge_protection.fault==fault && TIM1->CCR1==0 && charge_protection.inhibited);
    }
    puts("PASS: cooldown never clears ADC or failed-output faults");

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
