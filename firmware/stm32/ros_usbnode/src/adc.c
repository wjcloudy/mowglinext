/****************************************************************************
* Title                 :   adc module
* Filename              :   adc.c
* Author                :   Nekraus
* Origin Date           :   01/04/2023
* Version               :   1.0.0

*****************************************************************************/
/** \file adc.c
 *  \brief
 *
 */
/******************************************************************************
 * Includes
 *******************************************************************************/
#include "main.h"
#include "board.h"
#include "perimeter.h"
#include "adc.h"
#include <math.h>
#include "charge_diag.h"

#define ADC_CHARGING_USES_DMA BOARD_YARDFORCE500_VARIANT_B
/******************************************************************************
 * Module Preprocessor Constants
 *******************************************************************************/
const float f_RTO = 10000;
const float beta = 3380;

/******************************************************************************
 * Module Preprocessor Macros
 *******************************************************************************/

/******************************************************************************
 * Module Typedefs
 *******************************************************************************/
typedef enum
{
    ADC_CHARGING_CHANNEL_CURRENT = 0,
    ADC_CHARGING_CHANNEL_CHARGEVOLTAGE,
    ADC_CHARGING_CHANNEL_BATTERYVOLTAGE,
    ADC_CHARGING_CHANNEL_CHARGERINPUTVOLTAGE,
    ADC_CHARGING_CHANNEL_NTC,
    ADC_CHARGING_CHANNEL_MAX,
} ADC_Charging_channelSelection_e;

/******************************************************************************
 * Module Variable Definitions
 *******************************************************************************/
TIM_HandleTypeDef TIM2_Handle; // Time Base for ADC
ADC_HandleTypeDef ADC_Charging_Handle;
#if BOARD_YARDFORCE500_VARIANT_B
DMA_HandleTypeDef hdma_adc1; // CLOUDY: charging ADC1 via DMA2_Stream0 (background scan)
#endif
RTC_HandleTypeDef hrtc = {0};

ADC_Charging_channelSelection_e adc_charging_eChannelSelection = ADC_CHARGING_CHANNEL_CURRENT;

volatile uint16_t adc_u16BatteryVoltage       = 0;
volatile uint16_t adc_u16Current              = 0;
volatile uint16_t adc_u16ChargerVoltage       = 0;
volatile uint16_t adc_u16ChargerInputVoltage  = 0;
volatile uint16_t adc_u16Input_NTC            = 0;
#if BOARD_YARDFORCE500_VARIANT_B
/* CLOUDY: how many full channel scans the circular DMA buffer holds. On LFP we
 * keep N recent scans so ADC_input() can boxcar-average the noisy battery and
 * charge-voltage columns (~sqrt(N) noise rejection ahead of the IIR); the F401
 * has no hardware oversampler. Stock keeps two rows so one completed scan is
 * always available while DMA overwrites the other; it does not average them. */
#if BOARD_YARDFORCE500B_LFP
#define ADC_DMA_OVERSAMPLE 8
#else
#define ADC_DMA_OVERSAMPLE 2
#endif
// CLOUDY: circular-DMA target for the full channel scan(s); ADC_input() reads it directly
volatile uint16_t adc_inputDmaBuf[ADC_DMA_OVERSAMPLE * ADC_CHARGING_CHANNEL_MAX] = {0};
#endif

float battery_voltage;
float charge_voltage;
float current;
float current_without_offset;
float ntc_voltage;
float blade_temperature;
float chargerInputVoltage;

union FtoU ampere_acc;
union FtoU charge_current_offset;

/* A stalled acquisition must not leave the charger regulating frozen data.
 * Faults latch until reboot. Completion means a whole channel scan (IRQ), or
 * a whole DMA ring, not merely a changed ADC value: a steady signal is valid. */
#define ADC_CHARGING_TIMEOUT_MS 30u
static volatile uint8_t adc_charging_fault;
static volatile uint8_t adc_scan_seen;
static volatile uint32_t adc_last_scan_ms;
static uint8_t adc_input_ready;
static uint32_t adc_last_input_ms;
#if CHARGE_DIAGNOSTICS
#if !BOARD_YARDFORCE500B_LFP || !BOARD_YARDFORCE500_VARIANT_B
#error Charge diagnostics require the 500B LFP DMA profile
#endif
/* IRQ acknowledges TC; retain its sticky meaning for the existing foreground
 * health logic. Do not turn diagnostics into an ADC-timeout behavior change. */
static volatile uint8_t adc_diag_tc_pending;
uint8_t ADC_ChargingFaulted(void) { return adc_charging_fault; }

void DMA2_Stream0_IRQHandler(void)
{
    uint32_t ht = __HAL_DMA_GET_HT_FLAG_INDEX(&hdma_adc1);
    uint32_t tc = __HAL_DMA_GET_TC_FLAG_INDEX(&hdma_adc1);
    uint32_t flags = (__HAL_DMA_GET_FLAG(&hdma_adc1, ht) ? ht : 0u)
        | (__HAL_DMA_GET_FLAG(&hdma_adc1, tc) ? tc : 0u);
    uint32_t now = HAL_GetTick();
    __HAL_DMA_CLEAR_FLAG(&hdma_adc1, flags);
    if (flags & tc) adc_diag_tc_pending = 1;
    if (!flags) return;
    if (charge_diag.freeze_reason) return;
    /* Errors remain pending for ADC_ChargingHealthy; this handler never uses
     * HAL_DMA_IRQHandler (which would consume those health flags). */
    uint32_t remaining = __HAL_DMA_GET_COUNTER(&hdma_adc1);
    unsigned offset = remaining > 20u ? 20u : 0u;
    uint32_t expected = offset ? tc : ht;
    if ((flags & (ht | tc)) == (ht | tc)) ChargeDiag_MissedBatch();
    if (!(flags & expected) || !remaining) {
        ChargeDiag_MissedBatch();
        return;
    }
    uint16_t samples[20];
    __DMB();
    for (unsigned i = 0; i < 20; ++i) samples[i] = adc_inputDmaBuf[offset + i];
    __DMB();
    uint32_t after = __HAL_DMA_GET_COUNTER(&hdma_adc1);
    /* Never record a half that DMA started overwriting, including a whole
     * ring wrap while preempted. Each half spans four 2 ms scans (TIM2 toggle,
     * ADC rising edge only). The 4 ms rejection below is conservative. */
    if (!after || ((after > 20u) != (remaining > 20u))
        || __HAL_DMA_GET_FLAG(&hdma_adc1, ht | tc)
        || (uint32_t)(HAL_GetTick() - now) >= 4u) {
        ChargeDiag_MissedBatch();
        return;
    }
    ChargeDiag_RawBatch(now, samples);
}
#endif

uint8_t ADC_ChargingHealthy(void)
{
    /* Snapshot the ISR timestamp before reading time, so an interrupt cannot
     * supply a future timestamp and make unsigned subtraction look expired. */
    uint8_t scan_seen = adc_scan_seen;
    uint32_t last_scan = adc_last_scan_ms;
    uint32_t now = HAL_GetTick();
#if BOARD_YARDFORCE500_VARIANT_B
    if (__HAL_ADC_GET_FLAG(&ADC_Charging_Handle, ADC_FLAG_OVR))
        adc_charging_fault = 1;
#endif
#if ADC_CHARGING_USES_DMA
    uint32_t errors = __HAL_DMA_GET_TE_FLAG_INDEX(&hdma_adc1)
        | __HAL_DMA_GET_DME_FLAG_INDEX(&hdma_adc1)
        | __HAL_DMA_GET_FE_FLAG_INDEX(&hdma_adc1);
    if (__HAL_DMA_GET_FLAG(&hdma_adc1, errors)
        || !(hdma_adc1.Instance->CR & DMA_SxCR_EN))
        adc_charging_fault = 1;
#endif
    if ((scan_seen && (uint32_t)(now - last_scan) > ADC_CHARGING_TIMEOUT_MS)
        || (adc_input_ready && (uint32_t)(now - adc_last_input_ms) > ADC_CHARGING_TIMEOUT_MS))
        adc_charging_fault = 1;
    return adc_input_ready && !adc_charging_fault;
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &ADC_Charging_Handle) adc_charging_fault = 1;
}

/******************************************************************************
 * Function Prototypes
 *******************************************************************************/
void adc_charging_SetChannel(ADC_Charging_channelSelection_e channel);

/******************************************************************************
 *  Public Functions
 *******************************************************************************/

/**
 * @brief TIM2 Initialization Function
 *
 * Used to start ADC every 250µs
 *
 * @param None
 * @retval None
 */
void TIM2_Init(void)
{

    /* USER CODE BEGIN TIM2_Init 0 */
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* USER CODE END TIM2_Init 0 */

    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    /* USER CODE BEGIN TIM2_Init 1 */

    /* USER CODE END TIM2_Init 1 */
    TIM2_Handle.Instance = TIM2;
    TIM2_Handle.Init.Prescaler = 18 - 1; // 72Mhz -> 4Mhz
    TIM2_Handle.Init.CounterMode = TIM_COUNTERMODE_UP;
#if BOARD_YARDFORCE500_VARIANT_B
    TIM2_Handle.Init.Period = 4000 - 1; // CLOUDY slower trigger so a full multi-channel DMA scan fits comfortably
#else
    TIM2_Handle.Init.Period = 1000 - 1; /*1khz*/
#endif
    TIM2_Handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    TIM2_Handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_OC_Init(&TIM2_Handle) != HAL_OK)
    {
        Error_Handler();
    }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&TIM2_Handle, &sClockSourceConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&TIM2_Handle, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfigOC.OCMode = TIM_OCMODE_TOGGLE;
    sConfigOC.Pulse = 5;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_OC_ConfigChannel(&TIM2_Handle, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }
    /* USER CODE BEGIN TIM2_Init 2 */

    /* USER CODE END TIM2_Init 2 */
}

/**
 * @brief ADC Initialization Function
 *
 * Init GPIO to read
 *
 * @param None
 * @retval None
 */
void ADC_Charging_Init(void)
{
	// Configuration: ADC1 for Yardforce 500B
	// 				  ADC2 for Yardforce 500 original
#if BOARD_YARDFORCE500_VARIANT_ORIG
    __HAL_RCC_ADC2_CLK_ENABLE();
	ADC_TypeDef *Charging_ADC = ADC2;
#elif BOARD_YARDFORCE500_VARIANT_B
	__HAL_RCC_ADC1_CLK_ENABLE();
	ADC_TypeDef *Charging_ADC = ADC1;
#endif
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    /**ADC1 GPIO Configuration
    PA1     ------> Charge Current
    PA2     ------> Charge Voltage
    PA3     ------> Battery Voltage
    PA7     ------> Charger Voltage
    PC2     ------>  Blade NTC
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* USER CODE BEGIN ADC1_Init 0 */

    /* USER CODE END ADC1_Init 0 */

    /* USER CODE BEGIN ADC1_Init 1 */

    /* USER CODE END ADC1_Init 1 */

    /** Common config
     */
    ADC_Charging_Handle.Instance = Charging_ADC;
	// technically ADC_SCAN_DISABLE on STM32f1, but this is compatible with STM32f1 und STM32f4, and zero is zero
	ADC_Charging_Handle.Init.ScanConvMode = DISABLE;
	ADC_Charging_Handle.Init.ContinuousConvMode = DISABLE;
	ADC_Charging_Handle.Init.DiscontinuousConvMode = DISABLE;
	ADC_Charging_Handle.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_CC2;
	ADC_Charging_Handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	ADC_Charging_Handle.Init.NbrOfConversion = 1;

#if BOARD_YARDFORCE500_VARIANT_B
	ADC_Charging_Handle.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
	ADC_Charging_Handle.Init.Resolution = ADC_RESOLUTION_12B;
	ADC_Charging_Handle.Init.ScanConvMode = ENABLE;                      // CLOUDY scan all channels per trigger
	ADC_Charging_Handle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
    ADC_Charging_Handle.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_CC2;
	ADC_Charging_Handle.Init.NbrOfConversion = ADC_CHARGING_CHANNEL_MAX; // CLOUDY full scan -> DMA buffer
	ADC_Charging_Handle.Init.DMAContinuousRequests = ENABLE;             // CLOUDY keep DMA fed across triggers (circular)
	ADC_Charging_Handle.Init.EOCSelection = ADC_EOC_SEQ_CONV;
#endif

    if (HAL_ADC_Init(&ADC_Charging_Handle) != HAL_OK)
    {
        Error_Handler();
    }

	adc_charging_eChannelSelection = ADC_CHARGING_CHANNEL_CURRENT;
#if BOARD_YARDFORCE500_VARIANT_B
	// CLOUDY put every channel into the scan sequence (ranks 1..MAX)
	while (adc_charging_eChannelSelection < ADC_CHARGING_CHANNEL_MAX)
	{
		adc_charging_SetChannel(adc_charging_eChannelSelection);
		adc_charging_eChannelSelection++;
	}
	adc_charging_eChannelSelection = ADC_CHARGING_CHANNEL_CURRENT;
#else
	adc_charging_SetChannel(adc_charging_eChannelSelection);
#endif

#if BOARD_YARDFORCE500_VARIANT_ORIG
	IRQn_Type used_ADC_irq = ADC1_2_IRQn;
    HAL_NVIC_SetPriority(used_ADC_irq, 0, 0);
    HAL_NVIC_EnableIRQ(used_ADC_irq);

    // calibrate  - important for accuracy !
    HAL_ADCEx_Calibration_Start(&ADC_Charging_Handle);
    if (HAL_ADC_Start_IT(&ADC_Charging_Handle) != HAL_OK)
        adc_charging_fault = 1;
#elif BOARD_YARDFORCE500_VARIANT_B
    // CLOUDY: drive the charging ADC by DMA instead of per-conversion interrupts.
    // ADC1 -> DMA2_Stream0 (channel 0), circular; one full channel scan per TIM2 trigger
    // lands in adc_inputDmaBuf[]. ADC_input() polls completion/error flags and
    // snapshots the ring; no ADC/DMA NVIC is enabled for this path.
    // (STM32f4 has no ADC calibration call.)
    __HAL_RCC_DMA2_CLK_ENABLE();
    hdma_adc1.Instance = DMA2_Stream0;
    hdma_adc1.Init.Channel = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
        Error_Handler();
    }
    __HAL_LINKDMA(&ADC_Charging_Handle, DMA_Handle, hdma_adc1);
    if (HAL_ADC_Start_DMA(&ADC_Charging_Handle, (uint32_t *)&adc_inputDmaBuf[0],
            ADC_DMA_OVERSAMPLE * ADC_CHARGING_CHANNEL_MAX) != HAL_OK)
        adc_charging_fault = 1;
    /* HAL starts DMA with interrupts, but this path polls the flags. In
     * particular, leave ADC OVR pending for the health check to observe. */
    __HAL_ADC_DISABLE_IT(&ADC_Charging_Handle, ADC_IT_OVR);
    __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE | DMA_IT_DME);
    __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_FE);
#if CHARGE_DIAGNOSTICS
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    __HAL_DMA_ENABLE_IT(&hdma_adc1, DMA_IT_HT | DMA_IT_TC);
#endif
#endif
    if (HAL_TIM_OC_Start(&TIM2_Handle, TIM_CHANNEL_2) != HAL_OK)
        adc_charging_fault = 1;

    /* USER CODE BEGIN RTC_MspInit 0 */
    __HAL_RCC_PWR_CLK_ENABLE();
    /* USER CODE END RTC_MspInit 0 */
    /* Enable BKP CLK enable for backup registers */

#if BOARD_YARDFORCE500_VARIANT_ORIG
	// The STM32f4 seems to not require this, but the STM32f1 does
	// TODO: Check if this is true
    __HAL_RCC_BKP_CLK_ENABLE();
#endif
    /* Peripheral clock enable */
    __HAL_RCC_RTC_ENABLE();
    /* USER CODE BEGIN RTC_MspInit 1 */
    HAL_PWR_EnableBkUpAccess();

    ampere_acc.u[0] = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);
    ampere_acc.u[1] = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);

    charge_current_offset.u[0] = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3);
    charge_current_offset.u[1] = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR4);
}

/**
 * @brief ADC Input Function
 *
 * get the raw data and transform to human readeable values (V,A,T)
 *
 * @param None
 * @retval None
 */

void ADC_input(void)
{
    float l_fTmp;
    (void)ADC_ChargingHealthy();
    if (adc_charging_fault) return;
#if !ADC_CHARGING_USES_DMA
    if (!adc_scan_seen) return;
#endif

#if BOARD_YARDFORCE500_VARIANT_B
    /* TC is sticky even though the circular NDTR counter wraps. Equal NDTR
     * readings across 10 ms therefore cannot hide a stopped DMA stream. */
#if CHARGE_DIAGNOSTICS
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint8_t completed = adc_diag_tc_pending;
    adc_diag_tc_pending = 0;
    __set_PRIMASK(primask);
    if (completed)
#else
    if (__HAL_DMA_GET_FLAG(&hdma_adc1, __HAL_DMA_GET_TC_FLAG_INDEX(&hdma_adc1)))
#endif
    {
#if !CHARGE_DIAGNOSTICS
        __HAL_DMA_CLEAR_FLAG(&hdma_adc1, __HAL_DMA_GET_TC_FLAG_INDEX(&hdma_adc1)
            | __HAL_DMA_GET_HT_FLAG_INDEX(&hdma_adc1));
#endif
        adc_last_scan_ms = HAL_GetTick();
        adc_scan_seen = 1;
    }
    if (!adc_scan_seen) return; // the entire ring must be initialized first

    uint16_t snapshot[ADC_DMA_OVERSAMPLE * ADC_CHARGING_CHANNEL_MAX];
    uint32_t remaining = 0;
    uint8_t copied = 0;
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        uint32_t started = HAL_GetTick();
        remaining = __HAL_DMA_GET_COUNTER(&hdma_adc1);
        __DMB();
        for (unsigned i = 0; i < ADC_DMA_OVERSAMPLE * ADC_CHARGING_CHANNEL_MAX; ++i)
            snapshot[i] = adc_inputDmaBuf[i];
        __DMB();
        /* Reject a moving ring, including an entire wrap while preempted.
         * Less than 1 ms is shorter than a complete ring on either profile. */
        if (remaining == __HAL_DMA_GET_COUNTER(&hdma_adc1)
            && HAL_GetTick() == started)
        {
            copied = 1;
            break;
        }
    }
    if (!copied) return; // retain previous values; health timeout still applies
    unsigned written = ADC_DMA_OVERSAMPLE * ADC_CHARGING_CHANNEL_MAX - remaining;
    unsigned complete = written / ADC_CHARGING_CHANNEL_MAX;
    unsigned latest = (complete + ADC_DMA_OVERSAMPLE - 1) % ADC_DMA_OVERSAMPLE;
    unsigned offset = latest * ADC_CHARGING_CHANNEL_MAX;
    uint16_t raw_battery = snapshot[offset + ADC_CHARGING_CHANNEL_BATTERYVOLTAGE];
    uint16_t raw_charger = snapshot[offset + ADC_CHARGING_CHANNEL_CHARGEVOLTAGE];
    uint16_t raw_current = snapshot[offset + ADC_CHARGING_CHANNEL_CURRENT];
    uint16_t raw_chargerInput = snapshot[offset + ADC_CHARGING_CHANNEL_CHARGERINPUTVOLTAGE];
    uint16_t raw_ntc = snapshot[offset + ADC_CHARGING_CHANNEL_NTC];
#if BOARD_YARDFORCE500B_LFP
    unsigned partial = written % ADC_CHARGING_CHANNEL_MAX;
    uint32_t batt_acc = 0, chg_acc = 0, count = 0;
    for (unsigned row = 0; row < ADC_DMA_OVERSAMPLE; ++row)
    {
        // Exclude the partially overwritten row from the voltage average.
        if (partial && row == complete) continue;
        batt_acc += snapshot[row * ADC_CHARGING_CHANNEL_MAX + ADC_CHARGING_CHANNEL_BATTERYVOLTAGE];
        chg_acc += snapshot[row * ADC_CHARGING_CHANNEL_MAX + ADC_CHARGING_CHANNEL_CHARGEVOLTAGE];
        ++count;
    }
    raw_battery = (uint16_t)(batt_acc / count);
    raw_charger = (uint16_t)(chg_acc / count);
#endif
#else
    /* Snapshot volatile ADC values under interrupt lock to avoid torn reads */
    __disable_irq();
    uint16_t raw_battery       = adc_u16BatteryVoltage;
    uint16_t raw_charger       = adc_u16ChargerVoltage;
    uint16_t raw_current       = adc_u16Current;
    uint16_t raw_chargerInput  = adc_u16ChargerInputVoltage;
    uint16_t raw_ntc           = adc_u16Input_NTC;
    __enable_irq();
#endif

    /* Start the IIRs at measured values, not zero, before enabling charging. */
    if (!adc_input_ready)
    {
        battery_voltage = ((float)raw_battery / 4095.0f) * 3.3f * 10.09f + 0.6f;
        charge_voltage = ((float)raw_charger / 4095.0f) * 3.3f * 16.0f;
        current_without_offset = (((float)raw_current / 4095.0f) * 3.3f - 2.5f) * 100.0f / 12.0f;
        ntc_voltage = ((float)raw_ntc / 4095.0f) * 3.3f;
        chargerInputVoltage = ((float)raw_chargerInput / 4095.0f) * 3.3f * 16.0f;
    }

    /* battery volatge calculation */
    l_fTmp = ((float)raw_battery / 4095.0f) * 3.3f * 10.09 + 0.6f;
#if BOARD_YARDFORCE500B_LFP
    //CLOUDY heavier IIR smoothing so a load-induced spike can't trip the CC<->CV logic
    battery_voltage = V_BATT_SMOOTH_ALPHA * l_fTmp + (1.0f - V_BATT_SMOOTH_ALPHA) * battery_voltage;
#else
    battery_voltage = 0.2 * l_fTmp + 0.8 * battery_voltage;
#endif

     /*charger voltage calculation */
    l_fTmp = ((float)raw_charger / 4095.0f) * 3.3f * 16;
#if BOARD_YARDFORCE500B_LFP
    //CLOUDY the PWM-switched charge rail is the noisiest signal - smooth it hard
    charge_voltage = V_CHARGE_SMOOTH_ALPHA * l_fTmp + (1.0f - V_CHARGE_SMOOTH_ALPHA) * charge_voltage;
#else
    charge_voltage = 0.8 * l_fTmp + 0.2 * charge_voltage;
#endif

    /*charge current calculation */
    l_fTmp = (((float)raw_current / 4095.0f) * 3.3f - 2.5f) * 100 / 12.0;
    current_without_offset =   0.8 * l_fTmp + 0.2 * current_without_offset;

    /*remove offset*/
    current = current_without_offset - charge_current_offset.f;

    /*blade motor temperature calculation */
    l_fTmp = (raw_ntc/4095.0f)*3.3f;
    ntc_voltage = 0.5*l_fTmp + 0.5*ntc_voltage;

    /*calculation for NTC temperature*/
    l_fTmp = ntc_voltage * 10000;               //Resistance of RT
    l_fTmp = log(l_fTmp / f_RTO);
    l_fTmp = (1 / ((l_fTmp / beta) + (1 / (273.15+25)))); //Temperature from thermistor
    blade_temperature = l_fTmp - 273.15;                 //Conversion to Celsius

    /* Input voltage from the external supply*/
    l_fTmp = (raw_chargerInput / 4095.0f) * 3.3f * (32 / 2);
    chargerInputVoltage = 0.5 * l_fTmp + 0.5 * chargerInputVoltage;
    adc_last_input_ms = HAL_GetTick();
    adc_input_ready = 1;

}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
#ifdef OPTION_PERIMETER
    if (hadc == &ADC_Handle)
    {
        PERIMETER_vITHandle();
    }
#endif

    if (hadc == &ADC_Charging_Handle)
    {
#if BOARD_YARDFORCE500_VARIANT_ORIG
        /* CLOUDY VARIANT_B reads the charging ADC via the DMA buffer, so this
           per-conversion ISR path is ORIG-only. */
        uint16_t l_u16Rawdata = ADC_Charging_Handle.Instance->DR;

        switch (adc_charging_eChannelSelection)
        {
        case ADC_CHARGING_CHANNEL_CURRENT:
            adc_u16Current = l_u16Rawdata;
            break;

        case ADC_CHARGING_CHANNEL_CHARGEVOLTAGE:
            adc_u16ChargerVoltage = l_u16Rawdata;
            break;

        case ADC_CHARGING_CHANNEL_BATTERYVOLTAGE:
            adc_u16BatteryVoltage = l_u16Rawdata;
            break;

        case ADC_CHARGING_CHANNEL_CHARGERINPUTVOLTAGE:
            adc_u16ChargerInputVoltage = l_u16Rawdata;
            break;

        case ADC_CHARGING_CHANNEL_NTC:
            adc_u16Input_NTC = l_u16Rawdata;
            adc_last_scan_ms = HAL_GetTick();
            adc_scan_seen = 1;

            break;

        case ADC_CHARGING_CHANNEL_MAX:
        default:
            /* should not get here */
            break;
        }

        adc_charging_eChannelSelection++;
        if (adc_charging_eChannelSelection == ADC_CHARGING_CHANNEL_MAX)
			adc_charging_eChannelSelection = ADC_CHARGING_CHANNEL_CURRENT;
		adc_charging_SetChannel(adc_charging_eChannelSelection);

        if (HAL_ADC_Start_IT(&ADC_Charging_Handle) != HAL_OK)
            adc_charging_fault = 1;
#endif
    }
}



/******************************************************************************
 *  Private Functions
 *******************************************************************************/

void adc_charging_SetChannel(ADC_Charging_channelSelection_e channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

#if BOARD_YARDFORCE500_VARIANT_ORIG
	uint32_t adc_SampleTime = ADC_SAMPLETIME_239CYCLES_5;
	uint32_t rank = 1;
#elif BOARD_YARDFORCE500_VARIANT_B
	uint32_t adc_SampleTime = ADC_SAMPLETIME_480CYCLES;
	uint32_t rank = 1 + (uint32_t)channel; // CLOUDY scan-sequence position for the DMA multi-channel scan
#endif

    switch (channel)
    {
    case ADC_CHARGING_CHANNEL_CURRENT:
        sConfig.Channel = ADC_CHANNEL_1; // PA1 Charge Current
        sConfig.Rank = rank;
        sConfig.SamplingTime = adc_SampleTime;
        if (HAL_ADC_ConfigChannel(&ADC_Charging_Handle, &sConfig) != HAL_OK)
        {
            Error_Handler();
        }
        break;

    case ADC_CHARGING_CHANNEL_CHARGEVOLTAGE:
        sConfig.Channel = ADC_CHANNEL_2; // PA2 Charge Voltage
        sConfig.Rank = rank;
        sConfig.SamplingTime = adc_SampleTime;
        if (HAL_ADC_ConfigChannel(&ADC_Charging_Handle, &sConfig) != HAL_OK)
        {
            Error_Handler();
        }
        break;

    case ADC_CHARGING_CHANNEL_BATTERYVOLTAGE:
        sConfig.Channel = ADC_CHANNEL_3; // PA3 Battery
        sConfig.Rank = rank;
        sConfig.SamplingTime = adc_SampleTime;
        if (HAL_ADC_ConfigChannel(&ADC_Charging_Handle, &sConfig) != HAL_OK)
        {
            Error_Handler();
        }
        break;

    case ADC_CHARGING_CHANNEL_CHARGERINPUTVOLTAGE:
        sConfig.Channel = ADC_CHANNEL_7; // PA7 Charger Input voltage
        sConfig.Rank = rank;
        sConfig.SamplingTime = adc_SampleTime;
        if (HAL_ADC_ConfigChannel(&ADC_Charging_Handle, &sConfig) != HAL_OK)
        {
            Error_Handler();
        }
        break;

    case ADC_CHARGING_CHANNEL_NTC:
        sConfig.Channel = ADC_CHANNEL_13; // PC2
        sConfig.Rank = rank;
        sConfig.SamplingTime = adc_SampleTime;
        if (HAL_ADC_ConfigChannel(&ADC_Charging_Handle, &sConfig) != HAL_OK)
        {
            Error_Handler();
        }
        break;

    case ADC_CHARGING_CHANNEL_MAX:
    default:
        /* should not get here */
        sConfig.Channel = ADC_CHANNEL_3; // PA3 Battery
        sConfig.Rank = rank;
        sConfig.SamplingTime = adc_SampleTime;
        if (HAL_ADC_ConfigChannel(&ADC_Charging_Handle, &sConfig) != HAL_OK)
        {
            Error_Handler();
        }
        break;
    }
}
