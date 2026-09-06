/****************************************************************************
* Title                 :   charger module
* Filename              :   charger.c
* Author                :   Nekraus
* Origin Date           :   01/04/2023
* Version               :   1.0.0

*****************************************************************************/
/** \file charger.c
 *  \brief
 *
 */
/******************************************************************************
 * Includes
 *******************************************************************************/
#include "main.h"
#include "board.h"
#include "adc.h"
#include "charger.h"
#include "charge_diag.h"
#include "charge_protection.h"
/******************************************************************************
 * Module Preprocessor Constants
 *******************************************************************************/

/******************************************************************************
 * Module Preprocessor Macros
 *******************************************************************************/

/******************************************************************************
 * Module Typedefs
 *******************************************************************************/

typedef enum{
    CHARGER_STATE_IDLE,
    CHARGER_STATE_CONNECTED,
    CHARGER_STATE_CHARGING_CC,
    CHARGER_STATE_CHARGING_CV,
    CHARGER_STATE_END_CHARGING,
} CHARGER_STATE_e;

/******************************************************************************
 * Module Variable Definitions
 *******************************************************************************/

TIM_HandleTypeDef TIM1_Handle;  // PWM Charge Controller

float SOC                           = 0;
uint16_t chargecontrol_pwm_val      = 0;
uint8_t  chargecontrol_is_charging  = 0;

static CHARGER_STATE_e charger_state = CHARGER_STATE_IDLE;
static float charge_end_voltage=BAT_CHARGE_CUTOFF_VOLTAGE ;
#if BOARD_YARDFORCE500B_LFP
static uint16_t cv_entry_debounce = 0; //CLOUDY consecutive CC cycles with battery_voltage >= trip
volatile charge_protection_t charge_protection = { .version = 2, .inhibited = 1 };
static uint8_t charge_pwm_started;
static uint32_t charger_last_rise_ms, charger_started_ms, output_suspect_since;
static uint8_t output_suspect;

/* CLOUDY: loss detection is an acquisition-side, zero-only override. Never
 * release here: a low row followed by high rows in one DMA batch must still
 * force the foreground state machine through a new zero-duty startup. */
void Charger_InputInvalid(void)
{
    if (!charge_protection.inhibited && charge_protection.starts) {
        ++charge_protection.losses;
        charge_protection.restart_pending = 1;
    }
    charge_protection.inhibited = 1;
    charge_protection.qualified = 0;
    if (charge_pwm_started) TIM1->CCR1 = 0;
}

void Charger_InputSample(uint16_t raw, uint32_t now)
{
    if (charge_protection.input_seen &&
        (uint32_t)(now - charge_protection.last_input_ms) > CHARGE_INPUT_FRESH_MS)
        Charger_InputInvalid();
    charge_protection.input_seen = 1;
    charge_protection.last_input_ms = now;
    charge_protection.raw_input = raw;
    if (raw <= CHARGE_INPUT_LOSS_RAW) {
        Charger_InputInvalid();
    } else if (charge_protection.inhibited) {
        if (raw < CHARGE_INPUT_RECOVER_RAW) charge_protection.qualified = 0;
        else if (!charge_protection.qualified) {
            charge_protection.stable_since = now;
            charge_protection.qualified = 1;
        }
    }
}

/* Called with interrupts masked, just like the final PWM commit. Elapsed time
 * and fresh raw samples qualify restart, not filtered voltage or call counts.
 * ADC/output faults latch until MCU reboot; contact retries only rate-limit. */
static uint8_t charger_allow_power(uint32_t now)
{
    if (!charge_protection.input_seen ||
        (uint32_t)(now - charge_protection.last_input_ms) > CHARGE_INPUT_FRESH_MS)
        Charger_InputInvalid();
    if (charge_protection.fault) return 0;
    /* CLOUDY: manual redocking/bounce must not strand a healthy charger until
     * reboot. Exhausting the contact budget starts a full off-time instead.
     * Contact changes cannot shorten or restart this timer. Fresh/stable input
     * below still gates release; no saved duty is reused on cooldown expiry.
     * This is NOT an automatic retry of an ADC or failed-output fault. */
    if (charge_protection.cooldown_active) {
        if ((uint32_t)(now - charge_protection.cooldown_since) < CHARGE_RESTART_COOLDOWN_MS)
            return 0;
        charge_protection.cooldown_active = 0;
        charge_protection.window_active = 0;
        charge_protection.restarts = 0;
    }
    if (!charge_protection.inhibited) return 1;
    if (!charge_protection.qualified ||
        (uint32_t)(now - charge_protection.stable_since) < CHARGE_INPUT_STABLE_MS)
        return 0;
    if (charge_protection.restart_pending) {
        if (!charge_protection.window_active ||
            (uint32_t)(now - charge_protection.restart_window_ms) >= CHARGE_RESTART_WINDOW_MS) {
            charge_protection.restart_window_ms = now;
            charge_protection.restarts = 0;
            charge_protection.window_active = 1;
        }
        if (charge_protection.restarts >= CHARGE_RESTART_LIMIT) {
            charge_protection.cooldown_active = 1;
            charge_protection.cooldown_since = now;
            ++charge_protection.cooldowns;
            return 0;
        }
        ++charge_protection.restarts;
    }
    charge_protection.restart_pending = 0;
    charge_protection.inhibited = 0;
    ++charge_protection.starts;
    return 2; /* New qualified connection: discard the previous PWM/state. */
}

static uint16_t charger_backoff_step(float over)
{
    return over > 0.5f ? 16 : over > 0.25f ? 6 : over > 0.1f ? 2 : 1;
}

static void charger_reduce_pwm(uint16_t step)
{
    /* The normal regulating floor must NEVER raise startup/zero duty. */
    uint16_t floor = chargecontrol_pwm_val >= MIN_PWM_VALUE ? MIN_PWM_VALUE : 0;
    chargecontrol_pwm_val = chargecontrol_pwm_val > floor + step
        ? chargecontrol_pwm_val - step : floor;
}

static void charger_increase_pwm(void)
{
    uint32_t now = HAL_GetTick();
    /* Protection may run faster than regulation. Never multiply ramp rate or
     * catch up missed increments after a delayed foreground update. */
    if ((uint32_t)(now - charger_last_rise_ms) >= 11u && chargecontrol_pwm_val < MAX_PWM_VALUE) {
        ++chargecontrol_pwm_val;
        charger_last_rise_ms = now;
    }
}
#endif

/* Runtime charge ceiling (PKT_ID_SET_SAFETY_LIMITS). Seeded with the compile-time
 * board_defaults.h values, which stay the power-on fallback AND the hard upper
 * bound the wire can never exceed (see charger_clamp_*): the host can only LOWER
 * the charge envelope, never overcharge. An unconnected host runs these vetted
 * defaults. */
static volatile float g_max_charge_voltage = (float)MAX_CHARGE_VOLTAGE;
static volatile float g_max_charge_current = (float)MAX_CHARGE_CURRENT;

/* Lower-only clamp to (floor, compiled ceiling]. Non-finite is rejected upstream
 * in the packet handler; an out-of-range value here falls back to the compiled
 * ceiling (invalid) or is capped to it (too high) — never above it. */
static float charger_clamp_voltage(float v) {
  if (v <= 0.0f) return (float)MAX_CHARGE_VOLTAGE;
  if (v < LOW_BAT_THRESHOLD) return (float)LOW_BAT_THRESHOLD;
  if (v > (float)MAX_CHARGE_VOLTAGE) return (float)MAX_CHARGE_VOLTAGE;
  return v;
}
static float charger_clamp_current(float i) {
  if (i <= 0.0f) return (float)MAX_CHARGE_CURRENT;
  if (i < 0.1f) return 0.1f;
  if (i > (float)MAX_CHARGE_CURRENT) return (float)MAX_CHARGE_CURRENT;
  return i;
}

void charger_set_charge_limits(float max_voltage, float max_current) {
  g_max_charge_voltage = charger_clamp_voltage(max_voltage);
  g_max_charge_current = charger_clamp_current(max_current);
}

/******************************************************************************
 * Function Prototypes
 *******************************************************************************/

/******************************************************************************
 *  Public Functions
 *******************************************************************************/


/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
 void TIM1_Init(void)
{
  /* USER CODE BEGIN TIM1_Init 0 */

  __HAL_RCC_TIM1_CLK_ENABLE();
  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  TIM1_Handle.Instance = TIM1;
  TIM1_Handle.Init.Prescaler = 0;
  TIM1_Handle.Init.CounterMode = TIM_COUNTERMODE_UP;
  TIM1_Handle.Init.Period = 1400;
  TIM1_Handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  TIM1_Handle.Init.RepetitionCounter = 0;
  TIM1_Handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&TIM1_Handle) != HAL_OK)
  {
    Error_Handler();
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&TIM1_Handle, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&TIM1_Handle, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 120;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&TIM1_Handle, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_ENABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_ENABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_1;
  sBreakDeadTimeConfig.DeadTime = 40;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_ENABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&TIM1_Handle, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

  GPIO_InitTypeDef GPIO_InitStruct = {0};  
  CHARGE_GPIO_CLK_ENABLE();
  /** TIM1 GPIO Configuration
  PA7 or PE8     -----> TIM1_CH1N
  PA8 oe PE9    ------> TIM1_CH1
  */
  GPIO_InitStruct.Pin = CHARGE_LOWSIDE_PIN|CHARGE_HIGHSIDE_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
#if BOARD_YARDFORCE500_VARIANT_B
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
#endif
  HAL_GPIO_Init(CHARGE_GPIO_PORT, &GPIO_InitStruct);

#if BOARD_YARDFORCE500_VARIANT_ORIG
  // TODO: Is something equivalent needed for the STM32f4?
  __HAL_AFIO_REMAP_TIM1_ENABLE();        // to use PE8/9 it is a full remap
#endif


    // Charge CH1/CH1N PWM Timer
  TIM1->CCR1 = 0;  
  HAL_TIM_PWM_Start(&TIM1_Handle, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&TIM1_Handle, TIM_CHANNEL_1);
#if BOARD_YARDFORCE500B_LFP
  charge_pwm_started = 1; /* ADC is initialized first; its hook must not start TIM1. */
#endif
  DB_TRACE(" * Charge Controler PWM Timers initialized\r\n");
}

 void charger_set_end_voltage(float v) {
    /* Limit input to reasonable values. */
    if (v>g_max_charge_voltage) {
      v=g_max_charge_voltage;
    } else if (v<LOW_BAT_THRESHOLD) {
      v=LOW_BAT_THRESHOLD;
    }
    /* Go back to constant current, if voltage is increased. */
    if (v>charge_end_voltage && charger_state==CHARGER_STATE_CHARGING_CV) {
      charger_state=CHARGER_STATE_CHARGING_CC;
    }
    charge_end_voltage=v;
 }

/*
 * manages the charge voltage, and charge, lowbat LED
 * improvementt need to be done to avoid sparks when connected charger and disconnected 
 * todo PID current measure
 * needs to be called frequently
 */
void ChargeController(void)
{                        
  static uint32_t timestamp = 0;
  /* Invalid/stale acquisition bypasses PWM floors and the dock debounce.
   * Do not integrate frozen current into the charge counter either. */
  if (!ADC_ChargingHealthy()) {
    charger_state = CHARGER_STATE_IDLE;
    chargecontrol_pwm_val = 0;
    chargecontrol_is_charging = 0;
    TIM1->CCR1 = 0;
#if CHARGE_DIAGNOSTICS
    ChargeDiag_Control(HAL_GetTick(), 0, 0, ADC_ChargingFaulted(),
        battery_voltage, charge_voltage, chargerInputVoltage, current,
        current_without_offset, blade_temperature);
#endif
#if BOARD_YARDFORCE500B_LFP
    cv_entry_debounce = 0;
    Charger_InputInvalid();
    if (ADC_ChargingFaulted()) charge_protection.fault = CHARGE_FAULT_ADC;
#endif
    return;
  }
  float charge_target = charge_end_voltage;
#if BOARD_YARDFORCE500B_LFP
  if (charge_target > g_max_charge_voltage) charge_target = g_max_charge_voltage;
  const float float_current_limit = (g_max_charge_current < FLOAT_CV_CURRENT)
      ? g_max_charge_current : FLOAT_CV_CURRENT;
#endif

  /* CLOUDY charging-only overlay: dock debounce must never retain PWM across
   * contact loss. Keep the acquire/release and final register commit paired. */
#if BOARD_YARDFORCE500B_LFP
  uint32_t irq_mask = __get_PRIMASK();
  __disable_irq();
  uint8_t allowed = charger_allow_power(HAL_GetTick());
  __set_PRIMASK(irq_mask);
  if (!allowed) {
#if CHARGE_DIAGNOSTICS
    ChargeDiag_Control(HAL_GetTick(), chargecontrol_pwm_val, charger_state,
        ADC_ChargingFaulted(), battery_voltage, charge_voltage, chargerInputVoltage,
        current, current_without_offset, blade_temperature);
    if (charge_protection.fault) ChargeDiag_Freeze(HAL_GetTick(), charge_protection.fault);
#endif
    charger_state = CHARGER_STATE_IDLE;
    chargecontrol_pwm_val = chargecontrol_is_charging = 0;
    TIM1->CCR1 = 0;
    cv_entry_debounce = 0;
    output_suspect = 0;
    goto charge_accounting; /* No TF4 cycling; valid off-dock current still counts. */
  }
  if (allowed == 2) {
    charger_state = CHARGER_STATE_CONNECTED;
    chargecontrol_pwm_val = 0;
    cv_entry_debounce = output_suspect = 0;
    timestamp = charger_started_ms = charger_last_rise_ms = HAL_GetTick();
  }
#else
  if(( chargerInputVoltage < MIN_DOCKED_VOLTAGE) ){
    charger_state = CHARGER_STATE_IDLE;
  }
#endif
    
    switch (charger_state)
    {
    case CHARGER_STATE_CONNECTED:
        
        /* when connected the 3.3v and 5v is provided by the charger so we get the real biais of the current measure */
        chargecontrol_pwm_val = 0;

        /* wait 100ms to read current */
        if( (HAL_GetTick() - timestamp) > 100){
#if BOARD_YARDFORCE500B_LFP
          //CLOUDY retain fixed Pi/electronics compensation instead of auto-zeroing at dock:
          //the Pi/electronics still draw current at the "zero" point, so auto-cal is wrong here.
          charge_current_offset.f = CURRENT_OFFSET;
#else
          charge_current_offset.f = current_without_offset;
#endif
          // Writes a data in a RTC Backup data Register 3&4
          HAL_PWR_EnableBkUpAccess();
          HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3, charge_current_offset.u[0]);    
          HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR4, charge_current_offset.u[1]);   
          HAL_PWR_DisableBkUpAccess(); 
          HAL_GPIO_WritePin(TF4_GPIO_PORT, TF4_PIN, 1); /* Power on the battery  Powerbus */
          charger_state = CHARGER_STATE_CHARGING_CC;
        }

        break;

    case CHARGER_STATE_CHARGING_CC:
#if BOARD_YARDFORCE500B_LFP
        // CLOUDY: retain asymmetric CC control (slow rise, proportional fall).
        // Hardware OCP remains a hypothesis, not something this loop can prove
        // or prevent at switching timescales. The contact guard bypasses the
        // regulating floor; reductions must never raise a startup duty below it.
        if ((current > (g_max_charge_current + 0.1f)) || (battery_voltage > charge_target))
        {
            charger_reduce_pwm(charger_backoff_step(current - g_max_charge_current));
        }
        if ((battery_voltage < charge_target) && (current < (g_max_charge_current - 0.1f))
            && (chargecontrol_pwm_val < MAX_PWM_VALUE))
        {
            charger_increase_pwm();
        }

        //CLOUDY switch to CV on the actual battery voltage (not the charge-rail voltage),
        //but DEBOUNCE it. LFP's IR drop at high charge current lifts the terminal voltage
        //~1.5 V above the resting/SoC voltage, so a transient trip must not latch CV while
        //the pack is only part full. Require the trip to hold CV_ENTRY_DEBOUNCE_CYCLES.
        if (battery_voltage >= charge_target) {
            if (cv_entry_debounce < CV_ENTRY_DEBOUNCE_CYCLES) {
                cv_entry_debounce++;
            }
        } else {
            cv_entry_debounce = 0;
        }
        if (cv_entry_debounce >= CV_ENTRY_DEBOUNCE_CYCLES) {
            cv_entry_debounce = 0;
            charger_state = CHARGER_STATE_CHARGING_CV;
        }
#else
        // cap charge current at 1.5 Amps
        if ((battery_voltage > charge_target && (chargecontrol_pwm_val > 0)) || ((current > g_max_charge_current) && (chargecontrol_pwm_val > 39)))
        {
            chargecontrol_pwm_val--;
        }
        if ((battery_voltage < charge_target) && (current < g_max_charge_current) && (chargecontrol_pwm_val < 1350))
        {
            chargecontrol_pwm_val++;
        }

        if(charge_voltage >= charge_target) {
            charger_state = CHARGER_STATE_CHARGING_CV;
        }
#endif

        break;

    case CHARGER_STATE_CHARGING_CV:
#if BOARD_YARDFORCE500B_LFP
    {
        //CLOUDY CV->CC fallback. If the (heavily smoothed) pack voltage has sagged well
        //below the CV trip, we latched CV early on a load-induced spike while the pack was
        //not actually full - resume bulk CC instead of floating at float_current_limit forever.
        //CC re-entry is debounced (CV_ENTRY_DEBOUNCE_CYCLES) so this won't thrash; raise
        //CV_EXIT_HYSTERESIS if you see top-of-charge hunting.
        if (battery_voltage < (charge_target - CV_EXIT_HYSTERESIS))
        {
            charger_state = CHARGER_STATE_CHARGING_CC;
        }
        else
        {
        //CLOUDY hold an LFP-friendly float voltage that never exceeds the runtime target.
        //(LFP must not be held at a high CV like Li-ion; float a little lower.)
        float cv_target = (charge_target < MAX_FLOAT_CV_VOLTAGE) ? charge_target : MAX_FLOAT_CV_VOLTAGE;

        //CLOUDY deadband around cv_target (+/- CV_VOLTAGE_DEADBAND): don't step PWM while the
        //pack is already near the float target. Without it the bang-bang nudges every cycle and
        //limit-cycles - and on the LFP's steep top-of-charge knee a tiny ripple becomes a big
        //terminal-voltage swing (observed 27.5<->28.7V at ~1Hz).
        if ((battery_voltage < (cv_target - CV_VOLTAGE_DEADBAND)) && (charge_voltage < (g_max_charge_voltage)) && (chargecontrol_pwm_val < MAX_PWM_VALUE))
        {
          //CLOUDY only push more current while we are below the float-current target
          if (current < float_current_limit) {
            charger_increase_pwm();
          }
        }
        if ((battery_voltage > (cv_target + CV_VOLTAGE_DEADBAND) && (chargecontrol_pwm_val > MIN_PWM_VALUE)) || (charge_voltage > (g_max_charge_voltage) && (chargecontrol_pwm_val > MIN_PWM_VALUE)))
        {
          if (current <= float_current_limit) charger_reduce_pwm(1);
        }

        //CLOUDY fixed float/CV current limit (was g_max_charge_current/10), floored at MIN_PWM_VALUE
        if (current > float_current_limit)
        {
            /* Same proportional priority as CC; do not spend 16 foreground
             * cycles removing 16 counts during a float-current excursion. */
            charger_reduce_pwm(charger_backoff_step(current - float_current_limit));
        }

        /* battery full ? */
        if (current < CHARGE_END_LIMIT_CURRENT) {
          //charger_state = CHARGER_STATE_END_CHARGING;
          /*consider as the battery full */
          ampere_acc.f = BATTERY_CAPACITY;
          SOC = 100;
        }
        }
    }
#else
        // set PWM to approach 29.4V  charge voltage
        if ((battery_voltage < charge_target) && (charge_voltage < (g_max_charge_voltage)) && (chargecontrol_pwm_val < 1350))
        {
          chargecontrol_pwm_val++;
        }
        if ((battery_voltage > charge_target && (chargecontrol_pwm_val > 0)) || (charge_voltage > (g_max_charge_voltage) && (chargecontrol_pwm_val > 39)))
        {
          chargecontrol_pwm_val--;
        }

        /* the current is limited to 150ma */
        if ((current > (g_max_charge_current/10)) && chargecontrol_pwm_val > 0)
        {
            chargecontrol_pwm_val--;
        }

        /* battery full ? */
        if (current < CHARGE_END_LIMIT_CURRENT) {
          //charger_state = CHARGER_STATE_END_CHARGING;
          /*consider as the battery full */
          ampere_acc.f = 2.8;
        }
#endif

        break;

    case CHARGER_STATE_END_CHARGING:

        chargecontrol_pwm_val = 0;

        break;


    case CHARGER_STATE_IDLE:
    default:
       
#if BOARD_YARDFORCE500B_LFP
        if (chargerInputVoltage >= MIN_DOCKED_VOLTAGE ) { //CLOUDY was hard-coded 30.0 (could miss a docked 8S LFP)
#else
        if (chargerInputVoltage >= 30.0 ) {
#endif
            charger_state = CHARGER_STATE_CONNECTED;
            HAL_GPIO_WritePin(TF4_GPIO_PORT, TF4_PIN, 0); /* Power off the battery  Powerbus */
            timestamp = HAL_GetTick();
        }
        chargecontrol_pwm_val = 0;
        break;
    }
    
#if BOARD_YARDFORCE500B_LFP
charge_accounting:
#endif
    ampere_acc.f += ((current - charge_current_offset.f)/(100*60*60));
#if BOARD_YARDFORCE500B_LFP
    if(ampere_acc.f >= BATTERY_CAPACITY)ampere_acc.f = BATTERY_CAPACITY;
    SOC = ampere_acc.f/BATTERY_CAPACITY;
#else
    if(ampere_acc.f >= 2.8)ampere_acc.f = 2.8;
    SOC = ampere_acc.f/2.8;
#endif

    // Writes a data in a RTC Backup data Register 1
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, ampere_acc.u[0]);    
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, ampere_acc.u[1]);   
    HAL_PWR_DisableBkUpAccess(); 

    chargecontrol_is_charging = charger_state;

    /*Check the PWM value for safety */
#if BOARD_YARDFORCE500B_LFP
    if (chargecontrol_pwm_val > MAX_PWM_VALUE){  //CLOUDY raised from 1350 (TIM1 period is 1400)
        chargecontrol_pwm_val = MAX_PWM_VALUE;
    }
#else
    if (chargecontrol_pwm_val > 1350){
        chargecontrol_pwm_val = 1350;
    }
#endif
#if BOARD_YARDFORCE500B_LFP
    /* Output and input are separate ADC rails. A healthy dock input does not
     * establish delivered charge. Retain pre-shutdown PWM in the recorder. */
    uint32_t now = HAL_GetTick();
    uint8_t failed_output = (charger_state == CHARGER_STATE_CHARGING_CC ||
        charger_state == CHARGER_STATE_CHARGING_CV) && chargecontrol_pwm_val >= 1200 &&
        (uint32_t)(now - charger_started_ms) >= 2000u && chargerInputVoltage >= 22.0f &&
        battery_voltage > 20.0f && charge_voltage < battery_voltage * 0.5f && current < 0.0f;
    if (failed_output) {
        if (!output_suspect) { output_suspect = 1; output_suspect_since = now; }
        if ((uint32_t)(now - output_suspect_since) >= 250u) {
#if CHARGE_DIAGNOSTICS
            ChargeDiag_Control(now, chargecontrol_pwm_val, charger_state, ADC_ChargingFaulted(),
                battery_voltage, charge_voltage, chargerInputVoltage, current,
                current_without_offset, blade_temperature);
            ChargeDiag_Freeze(now, CHARGE_FAULT_OUTPUT);
#endif
            charge_protection.fault = CHARGE_FAULT_OUTPUT;
            Charger_InputInvalid();
        }
    } else output_suspect = 0;

    /* The input IRQ can interrupt all calculations above. Test the inhibit
     * again under the SAME IRQ mask as the write, so no stale high-duty write
     * can undo its zero-only override. Do not remove this on upstream merges. */
    irq_mask = __get_PRIMASK();
    __disable_irq();
    if (charge_protection.inhibited || charge_protection.fault) {
        chargecontrol_pwm_val = chargecontrol_is_charging = 0;
        charger_state = CHARGER_STATE_IDLE;
    }
    TIM1->CCR1 = chargecontrol_pwm_val;
    __set_PRIMASK(irq_mask);
#else
    TIM1->CCR1 = chargecontrol_pwm_val;
#endif
#if CHARGE_DIAGNOSTICS
    ChargeDiag_Control(HAL_GetTick(), chargecontrol_pwm_val, charger_state,
        ADC_ChargingFaulted(), battery_voltage, charge_voltage,
        chargerInputVoltage, current, current_without_offset, blade_temperature);
#endif
    
}

/******************************************************************************
 *  Private Functions
 *******************************************************************************/
