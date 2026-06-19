

#ifndef __BOARD_H
#define __BOARD_H

#ifdef __cplusplus
extern "C"
{
#endif

    // this is the sofware version that any other Mowgli components like MowgliRover will match against

#define MOWGLI_SW_VERSION_MAJOR 1
#define MOWGLI_SW_VERSION_BRANCH 11 /* even = stable, odd = testing/unstable */
#define MOWGLI_SW_VERSION_MINOR 1

/********************************************************************************
 * BOARD SELECTION
 * the specific board setting are set a the end of this file
 ********************************************************************************/
//#define BOARD_LUV1000RI 1

/* definition type don't modify */
#define DEBUG_TYPE_NONE 0
#define DEBUG_TYPE_UART 1
#define DEBUG_TYPE_SWO 2

/* Publish Mowgli Topics */
//#define ROS_PUBLISH_MOWGLI

/* different type of panel are possible */
#define PANEL_TYPE_NONE 0
#define PANEL_TYPE_YARDFORCE_500_CLASSIC 1
#define PANEL_TYPE_YARDFORCE_LUV1000RI 2
#define PANEL_TYPE_YARDFORCE_900_ECO 3
#define PANEL_TYPE_YARDFORCE_500B_CLASSIC 4

#if BOARD_YARDFORCE500_VARIANT_ORIG
///////////////////////////
// Yardforce 500 CLASSIC //
///////////////////////////
#define BLADEMOTOR_USART_INSTANCE USART3

#define VALID_BOARD_DEFINED 1
#define PANEL_TYPE PANEL_TYPE_YARDFORCE_500_CLASSIC
#define BLADEMOTOR_LENGTH_RECEIVED_MSG 16
#define DEBUG_TYPE DEBUG_TYPE_UART

#define MAX_MPS 0.5		  // Allow maximum speed of 1.0 m/s
#define PWM_PER_MPS 300.0 // PWM value of 300 means 1 m/s bot speed so we divide by 4 to have correct robot speed but still progressive speed
#define TICKS_PER_M 300.0 // Motor Encoder ticks per meter
#define WHEEL_BASE  0.325		// The distance between the center of the wheels in meters

#define OPTION_ULTRASONIC 0
#define OPTION_BUMPER 0

#define BOARD_HAS_MASTER_USART 1
#elif BOARD_YARDFORCE500_VARIANT_B
/////////////////////
// Yardforce 500 B //
/////////////////////

#define BLADEMOTOR_USART_INSTANCE USART6

#define VALID_BOARD_DEFINED 1
#define PANEL_TYPE PANEL_TYPE_YARDFORCE_500B_CLASSIC
#define BLADEMOTOR_LENGTH_RECEIVED_MSG 16
#define DEBUG_TYPE DEBUG_TYPE_SWO

#define MAX_MPS 0.5		  // Allow maximum speed of 1.0 m/s
#define PWM_PER_MPS 300.0 // PWM value of 300 means 1 m/s bot speed so we divide by 4 to have correct robot speed but still progressive speed
#define TICKS_PER_M 399.0 // Field-calibrated; 500B shares the 500-series drivetrain (was 300)
#define WHEEL_BASE  0.325		// The distance between the center of the wheels in meters

#define OPTION_ULTRASONIC 0
#if BOARD_YARDFORCE500B_LFP
#define OPTION_BUMPER 1 // CLOUDY LFP build: blue wheel-lift remapped as front bump sensor (gated on BOARD_YARDFORCE500B_LFP)
#else
#define OPTION_BUMPER 0
#endif

#define BOARD_HAS_MASTER_USART 0
#elif defined(BOARD_LUV1000RI) // TODO: This currently can't be selected via platformio
#define PANEL_TYPE PANEL_TYPE_YARDFORCE_LUV1000RI
#define BLADEMOTOR_LENGTH_RECEIVED_MSG 14

#define DEBUG_TYPE 0

#define OPTION_ULTRASONIC 1
#define OPTION_BUMPER 0

#define MAX_MPS 0.5		  // Allow maximum speed of 1.0 m/s
#define PWM_PER_MPS 300.0 // PWM value of 300 means 1 m/s bot speed so we divide by 4 to have correct robot speed but still progressive speed
#define TICKS_PER_M 300.0 // Motor Encoder ticks per meter
#define WHEEL_BASE 0.285   // The distance between the center of the wheels in meters

#define BOARD_HAS_MASTER_USART 0
#endif

//#define I_DONT_NEED_MY_FINGERS              1      // disables EmergencyController() (no wheel lift, or tilt sensing and stopping the blade anymore)

#if BOARD_YARDFORCE500B_LFP
/* ===========================================================================
 * CLOUDY LFP charging profile (transposed from the ROS1 500B mainboard build)
 * Pack: 8S LiFePO4, ~4.8 Ah  (stock firmware targets 7S Li-ion, ~2.8 Ah).
 * These are hard-coded (not GUI-config) so they survive both a direct
 * platformio build AND a GUI re-render of this file from board.h.template.
 * Keep board.h and board.h.template in sync.
 * =========================================================================== */

/// PWM safety ceiling (TIM1 period is 1400; stock cap was 1350).
/// An 8S LFP at ~28.5V on the ~29-30V rail has little buck headroom, so it
/// needs more duty to reach full current ("different charge-current/PWM curve").
#define MAX_PWM_VALUE 1395
/// PWM floor kept while still actively regulating (was the magic value 39)
#define MIN_PWM_VALUE 39
/// Fixed current-sensor offset [A]. Replaces auto-zeroing at dock, which is wrong
/// here because the Pi/electronics still draw current at the assumed "zero" point.
/// NOTE: re-measure on the actual hardware if the current reading looks biased.
#define CURRENT_OFFSET -0.20f
/// Usable pack capacity [Ah] for SOC coulomb counting (LFP voltage-based SOC is useless)
#define BATTERY_CAPACITY 4.8f

/// nominal max charge current [A] (~0.375C on a 4.8 Ah pack)
#define MAX_CHARGE_CURRENT 1.8f
/// Max charge-rail voltage allowed [V] (8S * 3.56 V/cell)
#define MAX_CHARGE_VOLTAGE 28.5f
/// CC->CV transition threshold [V] - kept for reference; the runtime
/// charge_end_voltage actually drives the transition (default = cutoff below)
#define LIMIT_VOLTAGE_150MA 28.5f
/// Default max battery voltage [V] - initial value of the runtime charge_end_voltage
#define BAT_CHARGE_CUTOFF_VOLTAGE 28.5f
/// CV/float battery ceiling [V] (8S * 3.44 V/cell). Effective target = min(this, charge_end_voltage).
#define MAX_FLOAT_CV_VOLTAGE 27.5f
/// CV/float current limit [A] - fixed, replaces the stock MAX_CHARGE_CURRENT/10.
/// CLOUDY raised 0.30 -> 0.40: 300mA couldn't cover the docked standby draw, so the pack
/// sagged off the 27.5V float until the 26.5V CC fallback re-engaged (slow sawtooth). This is
/// a cap; in CV the loop targets the float VOLTAGE and only draws what's needed to hold it.
#define FLOAT_CV_CURRENT 0.40f
/// We consider the battery full when CV current drops below this [A] (LFP has a flat tail)
#define CHARGE_END_LIMIT_CURRENT 0.25f
/// CLOUDY CC<->CV anti-latch. An 8S LFP has a very flat SoC curve and low ESR, so the
/// terminal voltage under ~1.8 A charge current sits ~1.5 V above the resting/SoC voltage.
/// Without these guards that loaded voltage trips CC->CV early and the one-way latch
/// (charger_set_end_voltage is never called) floats the pack at FLOAT_CV_CURRENT forever
/// while it is only part full (observed: stuck at ~0.3 A with the pack at 26.9 V).
/// Number of ChargeController cycles (~10 ms each) the CC->CV trip must hold before latching.
#define CV_ENTRY_DEBOUNCE_CYCLES 50
/// CV->CC fallback: only drop back to bulk CC if the (smoothed) pack voltage sags this far
/// below the CV trip - i.e. the pack is genuinely under-charged, NOT just floating. Must sit
/// well below where a full LFP floats (~27.5-28V), else normal float ripple bounces CV<->CC
/// and the pack hunts on its steep top-of-charge knee (observed 27.5<->28.7V at ~1Hz). [V]
#define CV_EXIT_HYSTERESIS 2.0f
/// CLOUDY CV voltage deadband: don't nudge PWM while within +/- this of the float target, so
/// the CV loop stops limit-cycling around it (the LFP knee turns a tiny ripple into a big
/// terminal-voltage swing). [V]
#define CV_VOLTAGE_DEADBAND 0.2f
/// CLOUDY ADC IIR smoothing weight on each NEW sample (~10 ms apart) for the voltages the
/// charge loop acts on. Lower = more smoothing / more lag. The PWM-switched charge rail and
/// the LFP pack's load-induced voltage swing are noisy, so smooth them hard (stock was
/// battery 0.2, charge rail 0.8).
#define V_BATT_SMOOTH_ALPHA   0.05f  /* ~190 ms time constant */
#define V_CHARGE_SMOOTH_ALPHA 0.1f   /* ~90 ms time constant (rail is the noisiest) */
// if charger-input voltage is greater than this assume we are docked [V]
#define MIN_DOCKED_VOLTAGE 22.0f
/// CLOUDY: ChargeController cycles (~10ms each) the charger input must stay below
/// MIN_DOCKED_VOLTAGE before we declare "undocked" and drop to IDLE (PWM 0). Debounced so a
/// transient input sag (e.g. a CC current overshoot, or a noisy reading) can't drop PWM to 0.
#define UNDOCK_DEBOUNCE_CYCLES 20
// if voltage is lower this assume battery is disconnected [V]
#define MIN_BATTERY_VOLTAGE 5.0f

// if current is greater than this assume the battery is charging [A]
#define MIN_CHARGE_CURRENT 0.1f
#define LOW_BAT_THRESHOLD 24.0f /* 8S LFP ~3.0 V/cell */
#define LOW_CRI_THRESHOLD 23.0f /* 8S LFP ~2.88 V/cell */
#else
/* ===== Stock 7S Li-ion charging profile (upstream defaults) ===== */
/// nominal max charge current is 1.0 Amp
#define MAX_CHARGE_CURRENT 1.0f
/// Max voltage allowed
#define MAX_CHARGE_VOLTAGE 29.0f
/// Voltage threshold for CC to CV transition
#define LIMIT_VOLTAGE_150MA 28.0f
/// Default max battery voltage allowed
#define BAT_CHARGE_CUTOFF_VOLTAGE  28.0f
/// We consider the battery is full when in CV mode the current below 0.1A
#define CHARGE_END_LIMIT_CURRENT 0.08f
// if voltage is greater than this assume we are docked
#define MIN_DOCKED_VOLTAGE 20.0f
// if voltage is lower this assume battery is disconnected
#define MIN_BATTERY_VOLTAGE 5.0f

// if current is greater than this assume the battery is charging
#define MIN_CHARGE_CURRENT 0.1f
#define LOW_BAT_THRESHOLD 25.2f /* near 20% SOC */
#define LOW_CRI_THRESHOLD 23.5f /* near 0% SOC */
#endif

// Emergency sensor timeouts
#define ONE_WHEEL_LIFT_EMERGENCY_MILLIS 2000
#define BOTH_WHEELS_LIFT_EMERGENCY_MILLIS 1000
#define TILT_EMERGENCY_MILLIS 500 // used for both the mechanical and accelerometer based detection
#define STOP_BUTTON_EMERGENCY_MILLIS 100
#define PLAY_BUTTON_CLEAR_EMERGENCY_MILLIS 2000
#if BOARD_YARDFORCE500B_LFP
// CLOUDY relaxed 0x38 (~26 deg, Z<0.896g) -> 0x2C (~45 deg, Z<0.704g, = stock). At 0x38 the
// onboard LIS3DH Z-low tilt INT false-tripped at the board's resting/dock orientation (chassis
// is level per the IMU, but the GForce board mounts at a slight tilt so its Z sat just under
// 0.896g), latching a TILT emergency on the dock that the host's release couldn't clear.
// 0x2C still protects against a real tip-over.
#define IMU_ONBOARD_INCLINATION_THRESHOLD 0x2C
#else
#define IMU_ONBOARD_INCLINATION_THRESHOLD 0x38 // stock firmware uses 0x2C (way more allowed inclination)
#endif

#if BOARD_YARDFORCE500B_LFP
// --- CLOUDY bump sensor: blue wheel-lift input remapped as a front bump sensor ---
// On this build Emergency_WheelLiftBlue() is disabled and HALLSTOP_Left/Right_Sense()
// read the blue wheel-lift pin instead (only one physical bump sensor). The remap is
// gated on BOARD_YARDFORCE500B_LFP directly in emergency.c / main.c / drivemotor.c.
// TEMPORARY: this drives a low-level reverse reflex only (drivemotor.c). The ROS2
// high level (Nav2 collision_monitor -> StuckBackoff) is meant to own obstacle
// recovery; plan to remove the firmware reflex once lidar/costmap recovery is trusted.
#define BUMP_MILLIS_WHILE_MOWING 100  // sustained-bump debounce while mowing [ms]
#define BUMP_MILLIS_WHILE_DOCKING 500 // sustained-bump debounce while docking [ms]
#define BUMP_REVERSE_MILLIS 1000      // how long to back off after a bump [ms] (was 2000; kept short on purpose)
#endif

// Enable Emergency debugging
//#define EMERGENCY_DEBUG

// IMU configuration options
#define EXTERNAL_IMU_ACCELERATION  1
#define EXTERNAL_IMU_ANGULAR       1

// Force disable IMU to be detected - CURRENTLY THIS SETTING DOES NOT WORK!
//#define DISABLE_LSM6
//#define DISABLE_MPU6050
//#define DISABLE_WT901

// we use J18 (Red 9 pin connector as Master Serial Port)
#define MASTER_J18 1

// enable Drive and Blade Motor UARTS
#define DRIVEMOTORS_USART_ENABLED 1
#define BLADEMOTOR_USART_ENABLED 1
#define PANEL_USART_ENABLED 1

// our IMU hangs of a bigbanged I2C bus on J18
#define SOFT_I2C_ENABLED 1

#define LED_PIN GPIO_PIN_2
#define LED_GPIO_PORT GPIOB
#define LED_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()

/* 24V Supply */
#define TF4_PIN GPIO_PIN_5
#define TF4_GPIO_PORT GPIOC
#define TF4_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()

/* Blade Motor nRESET - (HIGH for no RESET) */
#define PAC5223RESET_PIN GPIO_PIN_14
#define PAC5223RESET_GPIO_PORT GPIOE
#define PAC5223RESET_GPIO_CLK_ENABLE() __HAL_RCC_GPIOE_CLK_ENABLE()

/* Drive Motors - HC366 OE Pins (LOW to enable) */
#define PAC5210RESET_PIN GPIO_PIN_15
#define PAC5210RESET_GPIO_PORT GPIOE
#define PAC5210RESET_GPIO_CLK_ENABLE() __HAL_RCC_GPIOE_CLK_ENABLE()

/* Charge Control Pins - HighSide/LowSide MosFET */
#define CHARGE_LOWSIDE_PIN GPIO_PIN_8
#define CHARGE_HIGHSIDE_PIN GPIO_PIN_9
#define CHARGE_GPIO_PORT GPIOE
#define CHARGE_GPIO_CLK_ENABLE() __HAL_RCC_GPIOE_CLK_ENABLE();

/* Stop button - (HIGH when pressed) */
#define STOP_BUTTON_YELLOW_PIN GPIO_PIN_0
#define STOP_BUTTON_YELLOW_PORT GPIOC
#define STOP_BUTTON_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#define STOP_BUTTON_WHITE_PIN GPIO_PIN_8
#define STOP_BUTTON_WHITE_PORT GPIOC

/* Mechanical tilt - (HIGH when set) */
#define TILT_PIN GPIO_PIN_8
#define TILT_PORT GPIOA
#define TILT_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()

/* Wheel lift - (HIGH when set) */
#define WHEEL_LIFT_BLUE_PIN GPIO_PIN_0
#define WHEEL_LIFT_BLUE_PORT GPIOD
#define WHEEL_LIFT_GPIO_CLK_ENABLE() __HAL_RCC_GPIOD_CLK_ENABLE()
#define WHEEL_LIFT_RED_PIN GPIO_PIN_1
#define WHEEL_LIFT_RED_PORT GPIOD

/* Play button - (LOW when pressed) */
#if BOARD_YARDFORCE500_VARIANT_B
#define PLAY_BUTTON_PIN GPIO_PIN_9
#else
#define PLAY_BUTTON_PIN GPIO_PIN_7
#endif
#define PLAY_BUTTON_PORT GPIOC
#define PLAY_BUTTON_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()

/* Home button - (LOW when pressed) */
#define HOME_BUTTON_PIN GPIO_PIN_13
#define HOME_BUTTON_PORT GPIOB
#define HOME_BUTTON_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()


/* Rain Sensor - (LOW when active) */
#define RAIN_SENSOR_PIN GPIO_PIN_2
#define RAIN_SENSOR_PORT GPIOE
#define RAIN_SENSOR_GPIO_CLK_ENABLE() __HAL_RCC_GPIOE_CLK_ENABLE()

/* STOP HALL Sensor - (HIGH when set) */
#define HALLSTOP_RIGHT_PIN GPIO_PIN_2
#define HALLSTOP_LEFT_PIN GPIO_PIN_3
#define HALLSTOP_PORT GPIOD
#define HALLSTOP_GPIO_CLK_ENABLE() __HAL_RCC_GPIOD_CLK_ENABLE()

#if BOARD_HAS_MASTER_USART
    /* either J6 or J18 can be the master USART port */
#ifdef MASTER_J6
/* USART1 (J6 Pin 1 (TX) Pin 2 (RX)) */
#define MASTER_USART_INSTANCE USART1
#define MASTER_USART_RX_PIN GPIO_PIN_10
#define MASTER_USART_RX_PORT GPIOA
#define MASTER_USART_TX_PIN GPIO_PIN_9
#define MASTER_USART_TX_PORT GPIOA
#define MASTER_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define MASTER_USART_USART_CLK_ENABLE() __HAL_RCC_USART1_CLK_ENABLE()
#define MASTER_USART_IRQ USART1_IRQn
#endif
#ifdef MASTER_J18
/* UART4 (J18 Pin 7 (TX) Pin 8 (RX)) */
#define MASTER_USART_INSTANCE UART4
#define MASTER_USART_RX_PIN GPIO_PIN_11
#define MASTER_USART_RX_PORT GPIOC
#define MASTER_USART_TX_PIN GPIO_PIN_10
#define MASTER_USART_TX_PORT GPIOC
#define MASTER_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#define MASTER_USART_USART_CLK_ENABLE() __HAL_RCC_UART4_CLK_ENABLE()
#define MASTER_USART_IRQ UART4_IRQn
#endif
#endif

#ifdef DRIVEMOTORS_USART_ENABLED
/* drive motors PAC 5210 (USART2) */
#define DRIVEMOTORS_USART_INSTANCE USART2

#define DRIVEMOTORS_USART_RX_PIN GPIO_PIN_6
#define DRIVEMOTORS_USART_RX_PORT GPIOD

#define DRIVEMOTORS_USART_TX_PIN GPIO_PIN_5
#define DRIVEMOTORS_USART_TX_PORT GPIOD

#define DRIVEMOTORS_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOD_CLK_ENABLE()
#define DRIVEMOTORS_USART_USART_CLK_ENABLE() __HAL_RCC_USART2_CLK_ENABLE()

#define DRIVEMOTORS_USART_IRQ USART2_IRQn
#define DRIVEMOTORS_MSG_LEN 12
#endif

#ifdef BLADEMOTOR_USART_ENABLED
#if BOARD_YARDFORCE500_VARIANT_ORIG
/* blade motor PAC 5223 (USART3) */
#define BLADEMOTOR_USART_RX_PIN GPIO_PIN_11
#define BLADEMOTOR_USART_RX_PORT GPIOB

#define BLADEMOTOR_USART_TX_PIN GPIO_PIN_10
#define BLADEMOTOR_USART_TX_PORT GPIOB

#define BLADEMOTOR_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define BLADEMOTOR_USART_USART_CLK_ENABLE() __HAL_RCC_USART3_CLK_ENABLE()
#elif BOARD_YARDFORCE500_VARIANT_B
/* blade motor PAC 5223 (USART6) */
#define BLADEMOTOR_USART_RX_PIN GPIO_PIN_7
#define BLADEMOTOR_USART_RX_PORT GPIOC

#define BLADEMOTOR_USART_TX_PIN GPIO_PIN_6
#define BLADEMOTOR_USART_TX_PORT GPIOC

#define BLADEMOTOR_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#define BLADEMOTOR_USART_USART_CLK_ENABLE() __HAL_RCC_USART6_CLK_ENABLE()
#endif
#endif

#ifdef PANEL_USART_ENABLED
#define PANEL_USART_INSTANCE USART1

#define PANEL_USART_RX_PIN GPIO_PIN_10
#define PANEL_USART_RX_PORT GPIOA

#define PANEL_USART_TX_PIN GPIO_PIN_9
#define PANEL_USART_TX_PORT GPIOA

#define PANEL_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define PANEL_USART_USART_CLK_ENABLE() __HAL_RCC_USART1_CLK_ENABLE()
#define PANEL_USART_IRQ USART1_IRQn
#endif

// J18 has the SPI3 pins, as we dont use SPI3, we recycle them for I2C Bitbanging (for our IMU)
#ifdef SOFT_I2C_ENABLED
#define SOFT_I2C_SCL_PIN GPIO_PIN_3
#define SOFT_I2C_SCL_PORT GPIOB
#define SOFT_I2C_SDA_PIN GPIO_PIN_4
#define SOFT_I2C_SDA_PORT GPIOB

#define SOFT_I2C_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE();
#endif

#if !VALID_BOARD_DEFINED
#error "No valid board has been defined, this likely is a mismatch between this file and platformio.ini"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_H */
