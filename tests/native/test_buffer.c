#include <assert.h>
#include "../../firmware/frdmmcxn947_thermal_recovery_cm33_core0/source/thermal_buffer.h"
#include "../../firmware/frdmmcxn947_thermal_recovery_cm33_core0/source/thermal_logic.h"
static thermal_buffer_t b;
int main(void) {
for(unsigned i=1;i<=360;i++) ThermalBuffer_Push(&b,(thermal_sample_t){i,i*1000,27000,true});
assert(b.count==300 && b.dropped==60 && b.samples[b.head].seq==61);
ThermalBuffer_Acknowledge(&b,20);assert(b.count==300);
ThermalBuffer_Acknowledge(&b,61);assert(b.count==299 && b.samples[b.head].seq==62);
ThermalBuffer_Acknowledge(&b,61);assert(b.count==299);
ThermalBuffer_Acknowledge(&b,200);assert(b.count==160);
for(unsigned i=361;i<=500;i++) ThermalBuffer_Push(&b,(thermal_sample_t){i,i*1000,27000,true});
assert(b.count==300 && b.dropped==60);
for(unsigned i=201;i<=500;i++){assert(b.samples[b.head].seq==i);ThermalBuffer_Acknowledge(&b,i);}
assert(b.count==0);ThermalBuffer_Acknowledge(&b,501);assert(b.count==0);
assert(Thermal_ReportDue(true,10000,0,27000,27000,true,true));
assert(!Thermal_ReportDue(true,9999,0,27000,27000,true,true));
return 0;
}
