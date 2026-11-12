#include <assert.h>
#include "../../firmware/frdmmcxn947_thermal_recovery_cm33_core0/source/thermal_led.h"
static void colour(unsigned red,unsigned green,unsigned blue) {
    assert((!(outputs[0] & (1U<<10))) == red);
    assert((!(outputs[0] & (1U<<27))) == green);
    assert((!(outputs[1] & (1U<<2))) == blue);
}
int main(void) {
    ThermalLED_Init();
    assert(enabled_clocks==15 && configured_outputs==3 && configured_pads==3);
    colour(0,0,1); /* No sample yet. */
    ThermalLED_Update(true,false);colour(0,1,0);
    ThermalLED_Update(true,true);colour(1,0,0);
    ThermalLED_Update(false,true);colour(0,0,1); /* Invalid data is not a normal reading. */
    ThermalLED_Update(true,true);colour(1,0,0);
    ThermalLED_Update(true,false);colour(0,1,0);
    return 0;
}
