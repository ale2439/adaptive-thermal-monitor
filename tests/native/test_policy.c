#include <assert.h>
#include "../../firmware/frdmmcxn947_thermal_recovery_cm33_core0/source/thermal_logic.h"
int main(void) {
assert(Thermal_ReportDue(false,0,0,27000,27000,true,true));
assert(!Thermal_ReportDue(true,9999,0,27249,27000,true,true));
assert(Thermal_ReportDue(true,10000,0,27000,27000,true,true));
assert(Thermal_ReportDue(true,1000,0,27250,27000,true,true));
assert(Thermal_ReportDue(true,1000,0,26750,27000,true,true));
assert(Thermal_ReportDue(true,1000,0,27000,27000,false,true));
assert(Thermal_ReportDue(true,9990,0xfffffff0U,27000,27000,true,true));
assert(!Thermal_UpdateAlert(false,27999));
assert(Thermal_UpdateAlert(false,28000));
assert(Thermal_UpdateAlert(true,27801));
assert(!Thermal_UpdateAlert(true,27800));
return 0;
}
