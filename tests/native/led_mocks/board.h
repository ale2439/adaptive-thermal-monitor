#ifndef TEST_BOARD_H
#define TEST_BOARD_H
#include <stdint.h>
#define BOARD_LED_RED_GPIO 0
#define BOARD_LED_RED_GPIO_PIN 10U
#define BOARD_LED_GREEN_GPIO 0
#define BOARD_LED_GREEN_GPIO_PIN 27U
#define BOARD_LED_BLUE_GPIO 1
#define BOARD_LED_BLUE_GPIO_PIN 2U
#define LED_RED_OFF() GPIO_PortSet(0,1U<<10)
#define LED_RED_ON() GPIO_PortClear(0,1U<<10)
#define LED_GREEN_OFF() GPIO_PortSet(0,1U<<27)
#define LED_GREEN_ON() GPIO_PortClear(0,1U<<27)
#define LED_BLUE_OFF() GPIO_PortSet(1,1U<<2)
#define LED_BLUE_ON() GPIO_PortClear(1,1U<<2)
#endif
