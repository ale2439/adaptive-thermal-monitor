#ifndef THERMAL_LED_H
#define THERMAL_LED_H
#include <stdbool.h>
#include "board.h"
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_port.h"

/* FRDM-MCXN947 board.h: active-low R=P0_10, G=P0_27, B=P1_2.
 * Only startup and app_task write the RGB LED; MQTT callbacks never do.
 * Red/green reflect the local sensor even without Ethernet or a dashboard.
 */
static inline void ThermalLED_Update(bool sensor_valid, bool alert)
{
    /* All off before switching colours; prevents transient mixed colours. */
    LED_RED_OFF();
    LED_GREEN_OFF();
    LED_BLUE_OFF();
    if (!sensor_valid) { LED_BLUE_ON(); }
    else if (alert) { LED_RED_ON(); }
    else { LED_GREEN_ON(); }
}

static inline void ThermalLED_Init(void)
{
    CLOCK_EnableClock(kCLOCK_Port0);
    CLOCK_EnableClock(kCLOCK_Port1);
    CLOCK_EnableClock(kCLOCK_Gpio0);
    CLOCK_EnableClock(kCLOCK_Gpio1);
    const gpio_pin_config_t off = {kGPIO_DigitalOutput, 1U};
    const port_pin_config_t pin = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_SlowSlewRate,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAsGpio,
        .inputBuffer = kPORT_InputBufferDisable,
    };
    /* Preload high (off) before connecting GPIO output to each LED pad. */
    GPIO_PinInit(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN, &off);
    GPIO_PinInit(BOARD_LED_GREEN_GPIO, BOARD_LED_GREEN_GPIO_PIN, &off);
    GPIO_PinInit(BOARD_LED_BLUE_GPIO, BOARD_LED_BLUE_GPIO_PIN, &off);
    PORT_SetPinConfig(PORT0, BOARD_LED_RED_GPIO_PIN, &pin);
    PORT_SetPinConfig(PORT0, BOARD_LED_GREEN_GPIO_PIN, &pin);
    PORT_SetPinConfig(PORT1, BOARD_LED_BLUE_GPIO_PIN, &pin);
    ThermalLED_Update(false, false);
}
#endif
