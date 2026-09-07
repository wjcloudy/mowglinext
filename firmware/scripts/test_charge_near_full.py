#!/usr/bin/env python3
"""Characterize near-full behavior in the production charger, not a battery model.

These reproductions document why CC/max duty is possible; they do not assert
that the pack is full or that a hardware driver has tripped.
"""
import test_lfp_charger as test

test.TEST = test.TEST[:test.TEST.index('int main(void)')] + r'''
int main(void) {
    dock(); chargerInputVoltage=28.48f; battery_voltage=28.36f;
    charge_voltage=28.4f; current=0.0f;
    ticks(6000);
    assert(charger_state==CHARGER_STATE_CHARGING_CC);
    assert(chargecontrol_pwm_val==MAX_PWM_VALUE && !charge_protection.fault);
    assert(cv_entry_debounce==0);
    puts("REPRO: near-full voltage below 28.5 V remains in CC at maximum duty despite negligible current");

    dock(); current=0.2f;
    for (unsigned i=0;i<1000;++i) {
        battery_voltage=(i&1) ? 28.51f : 28.49f; tick();
    }
    assert(charger_state==CHARGER_STATE_CHARGING_CC);
    assert(cv_entry_debounce<=1);
    puts("REPRO: threshold crossings reset the 50-consecutive-sample CV qualification");

    dock(); battery_voltage=28.5f; ticks(50);
    assert(charger_state==CHARGER_STATE_CHARGING_CV);
    battery_voltage=27.5f; charge_voltage=27.5f; current=-0.2f;
    unsigned held=chargecontrol_pwm_val;
    ticks(500); assert(charger_state==CHARGER_STATE_CHARGING_CV);
    assert(chargecontrol_pwm_val==held && !charge_protection.fault);
    puts("PASS: established float at its target does not increase PWM for negative net current");

    Charger_InputSample(0,test_tick); ChargeController();
    assert(TIM1->CCR1==0);
    battery_voltage=28.36f; charge_voltage=28.4f; chargerInputVoltage=28.48f; current=0;
    ticks(6000);
    assert(charger_state==CHARGER_STATE_CHARGING_CC);
    assert(chargecontrol_pwm_val==MAX_PWM_VALUE && !charge_protection.fault);
    puts("REPRO: redocking a previously floating pack starts a new bulk cycle");

    // TIM1 upcounter: ARR=1400 gives 1401 counts. With CKD=DIV1 and DTG=40,
    // max duty leaves only 6 counts for OC1N, less than the 40-count delay.
    // RM0368 section 12.3.11: a pulse shorter than dead time is suppressed.
    assert(1401u-MAX_PWM_VALUE < 40u);
    assert(1401u-1350u > 40u);
    puts("REVIEW: configured 1395 cap suppresses the complementary pulse; stock 1350 leaves 11 counts after dead time");
    return 0;
}
'''

if __name__ == '__main__':
    test.main()
