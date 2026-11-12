#include <stdint.h>
enum {kGPIO_DigitalOutput};
typedef struct {unsigned pinDirection; unsigned outputLogic;} gpio_pin_config_t;
static uint32_t outputs[2];
static unsigned configured_outputs;
static void GPIO_PinInit(unsigned bank,unsigned pin,const gpio_pin_config_t *config) {
    assert(config->pinDirection==kGPIO_DigitalOutput && config->outputLogic==1);
    outputs[bank] |= 1U<<pin; configured_outputs++;
}
static void GPIO_PortSet(unsigned bank,uint32_t mask) {outputs[bank] |= mask;}
static void GPIO_PortClear(unsigned bank,uint32_t mask) {outputs[bank] &= ~mask;}
