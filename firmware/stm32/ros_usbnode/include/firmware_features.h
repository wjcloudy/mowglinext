#ifndef FIRMWARE_FEATURES_H
#define FIRMWARE_FEATURES_H

/* Acquisition and observation are independent build choices, not battery
 * profiles. Keep stock builds on their interrupt sampler unless opted in. */
#ifndef ADC_CHARGING_DMA
#define ADC_CHARGING_DMA 0
#endif
#ifndef CHARGE_DIAGNOSTICS
#define CHARGE_DIAGNOSTICS 0
#endif
#if (ADC_CHARGING_DMA != 0) && (ADC_CHARGING_DMA != 1)
#error ADC_CHARGING_DMA must be 0 or 1
#endif
#if (CHARGE_DIAGNOSTICS != 0) && (CHARGE_DIAGNOSTICS != 1)
#error CHARGE_DIAGNOSTICS must be 0 or 1
#endif
#if ADC_CHARGING_DMA && !BOARD_YARDFORCE500_VARIANT_B
#error Charging DMA is implemented only for the STM32F401 500B
#endif
#if CHARGE_DIAGNOSTICS && (!BOARD_YARDFORCE500_VARIANT_B || !BOARD_YARDFORCE500B_LFP)
#error Charge monitoring requires the 500B LFP profile
#endif
#endif
