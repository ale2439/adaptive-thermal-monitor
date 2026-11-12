/* Application policy. Thresholds are for the demonstration, not equipment limits. */
#ifndef THERMAL_LOGIC_H
#define THERMAL_LOGIC_H
#include <stdbool.h>
#include <stdint.h>
#define THERMAL_ALERT_ON_MC 28000
#define THERMAL_ALERT_OFF_MC 27800
static inline bool Thermal_UpdateAlert(bool active, int32_t temperature_mc)
{
    if (!active && temperature_mc >= THERMAL_ALERT_ON_MC) return true;
    if (active && temperature_mc <= THERMAL_ALERT_OFF_MC) return false;
    return active;
}
/* Sample once per second; report changes and a ten-second heartbeat. */
#define THERMAL_MAX_REPORT_MS 10000U
#define THERMAL_REPORT_DELTA_MC 250
static inline bool Thermal_ReportDue(bool reported, uint32_t now,
    uint32_t last_ms, int32_t temperature_mc, int32_t last_mc,
    bool alert, bool last_alert)
{
    int64_t delta = (int64_t)temperature_mc - last_mc;
    return !reported || alert != last_alert ||
        delta >= THERMAL_REPORT_DELTA_MC || delta <= -THERMAL_REPORT_DELTA_MC ||
        (uint32_t)(now - last_ms) >= THERMAL_MAX_REPORT_MS;
}
#endif
