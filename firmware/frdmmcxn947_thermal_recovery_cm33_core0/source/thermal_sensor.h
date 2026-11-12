/* Temperature sensor adapter; based on the included NXP P3T1755 example. */
#ifndef THERMAL_SENSOR_H
#define THERMAL_SENSOR_H
#include "fsl_common.h"
/* Called only by the application task, after BOARD_InitHardware(). */
status_t ThermalSensor_Init(void);
status_t ThermalSensor_Read(double *temperature);
#endif
