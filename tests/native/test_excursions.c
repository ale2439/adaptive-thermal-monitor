/* Deterministic ideal-input policy model. Not a physical temperature experiment. */
#include <assert.h>
#include <stdio.h>
#include "../../firmware/frdmmcxn947_thermal_recovery_cm33_core0/source/thermal_logic.h"
#include "../../firmware/frdmmcxn947_thermal_recovery_cm33_core0/source/thermal_buffer.h"

static void pending_ack_keeps_short_observed_excursion(void)
{
    thermal_buffer_t buffer = {0};
    bool recovering = false;
    ThermalBuffer_Push(&buffer, (thermal_sample_t){1,0,27500,false,false});
    /* A previous normal report is awaiting ACK when sampled ON and OFF occur. */
    ThermalBuffer_Push(&buffer, (thermal_sample_t){2,1000,28500,true,true});
    recovering = Thermal_KeepAlertHistory(recovering,true,true);
    ThermalBuffer_Push(&buffer, (thermal_sample_t){3,2000,27500,false,true});
    recovering = Thermal_KeepAlertHistory(recovering,true,true);
    assert(recovering);
    ThermalBuffer_Acknowledge(&buffer,1);
    assert(buffer.count==2 && buffer.samples[buffer.head].seq==2);
    assert(buffer.samples[buffer.head].alert && buffer.samples[buffer.head].alert_changed);
    ThermalBuffer_Acknowledge(&buffer,2);
    assert(buffer.count==1 && !buffer.samples[buffer.head].alert && buffer.samples[buffer.head].alert_changed);
    ThermalBuffer_Acknowledge(&buffer,3);
    assert(buffer.count==0);
}
int main(void)
{
    const unsigned widths[] = {100,200,500,1000,2000};
    const unsigned expected_detected[] = {1,2,5,10,10};
    pending_ack_keeps_short_observed_excursion();
    puts("Ideal-input model: 1 Hz sensing; 10 onset phases, spaced 100 ms.");
    puts("pulse_ms,phase_cases,sensor_detected,adaptive_reported,fixed_1hz_reported,adaptive_missed_total,max_detection_ms");
    for (unsigned k=0;k<5;k++) {
        unsigned sensor_detected=0, adaptive_reported=0, fixed_reported=0, max_delay=0;
        for (unsigned phase=0;phase<1000;phase+=100) {
            unsigned onset=10000+phase;
            bool active=false, reported=false, last_alert=false, sensed=false, sent=false;
            unsigned last_ms=0, detection_delay=0;
            int last_mc=0;
            for (unsigned now=0;now<=15000;now+=1000) {
                int mc=now>=onset && now<onset+widths[k] ? 28500 : 27500;
                active=Thermal_UpdateAlert(active,mc);
                if (active && !sensed) {sensed=true;detection_delay=now-onset;}
                bool due=Thermal_ReportDue(reported,now,last_ms,mc,last_mc,active,last_alert);
                if (due) {
                    if (active) sent=true;
                    reported=true;last_ms=now;last_mc=mc;last_alert=active;
                }
            }
            sensor_detected+=sensed;adaptive_reported+=sent;
            if (sensed && detection_delay>max_delay) max_delay=detection_delay;
        }
        /* Fixed 1 Hz transmits every sampled value: its detected-event count
           equals sensing detection, not the number of above-limit samples. */
        fixed_reported=sensor_detected;
        assert(sensor_detected==expected_detected[k]);
        assert(adaptive_reported==sensor_detected);
        printf("%u,10,%u,%u,%u,%u,%u\n",widths[k],sensor_detected,adaptive_reported,
            fixed_reported,10-adaptive_reported,max_delay);
    }
    return 0;
}
