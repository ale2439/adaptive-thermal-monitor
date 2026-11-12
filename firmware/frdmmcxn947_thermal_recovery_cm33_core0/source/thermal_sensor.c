/*
 * Copyright 2022, 2025-2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*  Standard C Included Files */
#include <string.h>
/*  SDK Included Files */
#include "fsl_debug_console.h"
#include "fsl_p3t1755.h"
#include "fsl_i3c.h"
#include "board.h"
#include "app.h"
#include "thermal_sensor.h"
#include "fsl_port.h"
#include "lwip/sys.h"

#define EXAMPLE_MASTER I3C1
#define I3C_MASTER_CLOCK_FREQUENCY CLOCK_GetI3cClkFreq(1)
#define SENSOR_SLAVE_ADDR 0x48U
#define EXAMPLE_I2C_BAUDRATE 400000U
#define EXAMPLE_I3C_OD_BAUDRATE 1500000U
#define EXAMPLE_I3C_PP_BAUDRATE 4000000U

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define I3C_BROADCAST_ADDR 0x7EU

#define SENSOR_ADDR 0x08U
#define CCC_RSTDAA  0x06U
#define CCC_SETDASA 0x87U

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void i3c_master_callback(I3C_Type *base, i3c_master_handle_t *handle, status_t status, void *userData);
static status_t I3C_WriteSensor(uint8_t deviceAddress, uint32_t regAddress, uint8_t *regData, size_t dataSize);
static status_t I3C_ReadSensor(uint8_t deviceAddress, uint32_t regAddress, uint8_t *regData, size_t dataSize);
static status_t p3t1755_set_dynamic_address(void);
/*******************************************************************************
 * Variables
 ******************************************************************************/
static volatile status_t g_completionStatus;
static volatile bool g_masterCompletionFlag;
static i3c_master_handle_t g_i3c_m_handle;
static p3t1755_handle_t p3t1755Handle;

static const i3c_master_transfer_callback_t masterCallback = {
    .slave2Master = NULL, .ibiCallback = NULL, .transferComplete = &i3c_master_callback};

/*******************************************************************************
 * Code
 ******************************************************************************/
static void i3c_master_callback(I3C_Type *base, i3c_master_handle_t *handle, status_t status, void *userData)
{
    if (status == kStatus_Success)
    {
        g_masterCompletionFlag = true;
    }

    g_completionStatus = status;
}

static status_t I3C_WriteSensor(uint8_t deviceAddress, uint32_t regAddress, uint8_t *regData, size_t dataSize)
{
    status_t result                  = kStatus_Success;
    i3c_master_transfer_t masterXfer = {0};
    uint32_t started = sys_now();

    masterXfer.slaveAddress   = deviceAddress;
    masterXfer.direction      = kI3C_Write;
    masterXfer.busType        = kI3C_TypeI3CSdr;
    masterXfer.subaddress     = regAddress;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = regData;
    masterXfer.dataSize       = dataSize;
    masterXfer.flags          = (uint32_t)kI3C_TransferDefaultFlag;

    g_masterCompletionFlag = false;
    g_completionStatus     = kStatus_Success;
    result                 = I3C_MasterTransferNonBlocking(EXAMPLE_MASTER, &g_i3c_m_handle, &masterXfer);
    if (kStatus_Success != result)
    {
        return result;
    }

    while (!g_masterCompletionFlag)
    {
        sys_msleep(1U);
        if ((g_completionStatus != kStatus_Success) || ((uint32_t)(sys_now() - started) >= 250U))
        {
            break;
        }
    }

    result = g_completionStatus;
    if ((uint32_t)(sys_now() - started) >= 250U)
    {
        I3C_MasterTransferAbort(EXAMPLE_MASTER, &g_i3c_m_handle);
        result = kStatus_Timeout;
    }

    return result;
}

static status_t I3C_ReadSensor(uint8_t deviceAddress, uint32_t regAddress, uint8_t *regData, size_t dataSize)
{
    status_t result                  = kStatus_Success;
    i3c_master_transfer_t masterXfer = {0};
    uint32_t started = sys_now();

    masterXfer.slaveAddress   = deviceAddress;
    masterXfer.direction      = kI3C_Read;
    masterXfer.busType        = kI3C_TypeI3CSdr;
    masterXfer.subaddress     = regAddress;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = regData;
    masterXfer.dataSize       = dataSize;
    masterXfer.flags          = (uint32_t)kI3C_TransferDefaultFlag;

    g_masterCompletionFlag = false;
    g_completionStatus     = kStatus_Success;
    result                 = I3C_MasterTransferNonBlocking(EXAMPLE_MASTER, &g_i3c_m_handle, &masterXfer);
    if (kStatus_Success != result)
    {
        return result;
    }

    while (!g_masterCompletionFlag)
    {
        sys_msleep(1U);
        if ((g_completionStatus != kStatus_Success) || ((uint32_t)(sys_now() - started) >= 250U))
        {
            break;
        }
    }

    result = g_completionStatus;
    if ((uint32_t)(sys_now() - started) >= 250U)
    {
        I3C_MasterTransferAbort(EXAMPLE_MASTER, &g_i3c_m_handle);
        result = kStatus_Timeout;
    }

    return result;
}

static status_t p3t1755_set_dynamic_address(void)
{
    status_t result                  = kStatus_Success;
    i3c_master_transfer_t masterXfer = {0};
    uint8_t txBuff[1];

    /* Reset dynamic address. */
    txBuff[0]               = CCC_RSTDAA;
    masterXfer.slaveAddress = I3C_BROADCAST_ADDR;
    masterXfer.data         = txBuff;
    masterXfer.dataSize     = 1;
    masterXfer.direction    = kI3C_Write;
    masterXfer.busType      = kI3C_TypeI3CSdr;
    masterXfer.flags        = (uint32_t)kI3C_TransferDefaultFlag;
    result                  = I3C_MasterTransferBlocking(EXAMPLE_MASTER, &masterXfer);
    if (result != kStatus_Success)
    {
        return result;
    }

    /* Assign dynmic address. */
    (void)memset(&masterXfer, 0, sizeof(masterXfer));
    txBuff[0]               = CCC_SETDASA;
    masterXfer.slaveAddress = I3C_BROADCAST_ADDR;
    masterXfer.data         = txBuff;
    masterXfer.dataSize     = 1;
    masterXfer.direction    = kI3C_Write;
    masterXfer.busType      = kI3C_TypeI3CSdr;
    masterXfer.flags        = (uint32_t)kI3C_TransferNoStopFlag;
    result                  = I3C_MasterTransferBlocking(EXAMPLE_MASTER, &masterXfer);
    if (result != kStatus_Success)
    {
        return result;
    }

    (void)memset(&masterXfer, 0, sizeof(masterXfer));
    txBuff[0]               = SENSOR_ADDR << 1;
    masterXfer.slaveAddress = SENSOR_SLAVE_ADDR;
    masterXfer.data         = txBuff;
    masterXfer.dataSize     = 1;
    masterXfer.direction    = kI3C_Write;
    masterXfer.busType      = kI3C_TypeI3CSdr;
    masterXfer.flags        = (uint32_t)kI3C_TransferDefaultFlag;
    return I3C_MasterTransferBlocking(EXAMPLE_MASTER, &masterXfer);
}


static void Thermal_InitSensorPins(void)
{
    /* Enables the clock for PORT1: Enables clock */
    CLOCK_EnableClock(kCLOCK_Port1);

    /* EFT detect interrupts configuration on PORT1_ */
    PORT_DisableEFTDetectInterrupts(PORT1, 0x030800u);

    const port_pin_config_t port1_11_pinD3_config = {/* Internal pull-up/down resistor is disabled */
                                                     .pullSelect = kPORT_PullDisable,
                                                     /* Low internal pull resistor value is selected. */
                                                     .pullValueSelect = kPORT_LowPullResistor,
                                                     /* Fast slew rate is configured */
                                                     .slewRate = kPORT_FastSlewRate,
                                                     /* Passive input filter is disabled */
                                                     .passiveFilterEnable = kPORT_PassiveFilterDisable,
                                                     /* Open drain output is disabled */
                                                     .openDrainEnable = kPORT_OpenDrainDisable,
                                                     /* Low drive strength is configured */
                                                     .driveStrength = kPORT_LowDriveStrength,
                                                     /* Pin is configured as I3C1_PUR */
                                                     .mux = kPORT_MuxAlt10,
                                                     /* Digital input enabled */
                                                     .inputBuffer = kPORT_InputBufferEnable,
                                                     /* Digital input is not inverted */
                                                     .invertInput = kPORT_InputNormal,
                                                     /* Pin Control Register fields [15:0] are not locked */
                                                     .lockRegister = kPORT_UnlockRegister};
    /* PORT1_11 (pin D3) is configured as I3C1_PUR */
    PORT_SetPinConfig(PORT1, 11U, &port1_11_pinD3_config);

    const port_pin_config_t port1_16_pinF6_config = {/* Internal pull-up resistor is enabled */
                                                     .pullSelect = kPORT_PullUp,
                                                     /* High internal pull resistor value is selected. */
                                                     .pullValueSelect = kPORT_HighPullResistor,
                                                     /* Fast slew rate is configured */
                                                     .slewRate = kPORT_FastSlewRate,
                                                     /* Passive input filter is disabled */
                                                     .passiveFilterEnable = kPORT_PassiveFilterDisable,
                                                     /* Open drain output is disabled */
                                                     .openDrainEnable = kPORT_OpenDrainDisable,
                                                     /* Low drive strength is configured */
                                                     .driveStrength = kPORT_LowDriveStrength,
                                                     /* Pin is configured as I3C1_SDA */
                                                     .mux = kPORT_MuxAlt10,
                                                     /* Digital input enabled */
                                                     .inputBuffer = kPORT_InputBufferEnable,
                                                     /* Digital input is not inverted */
                                                     .invertInput = kPORT_InputNormal,
                                                     /* Pin Control Register fields [15:0] are not locked */
                                                     .lockRegister = kPORT_UnlockRegister};
    /* PORT1_16 (pin F6) is configured as I3C1_SDA */
    PORT_SetPinConfig(PORT1, 16U, &port1_16_pinF6_config);

    const port_pin_config_t port1_17_pinF4_config = {/* Internal pull-up resistor is enabled */
                                                     .pullSelect = kPORT_PullUp,
                                                     /* Low internal pull resistor value is selected. */
                                                     .pullValueSelect = kPORT_LowPullResistor,
                                                     /* Fast slew rate is configured */
                                                     .slewRate = kPORT_FastSlewRate,
                                                     /* Passive input filter is disabled */
                                                     .passiveFilterEnable = kPORT_PassiveFilterDisable,
                                                     /* Open drain output is disabled */
                                                     .openDrainEnable = kPORT_OpenDrainDisable,
                                                     /* Low drive strength is configured */
                                                     .driveStrength = kPORT_LowDriveStrength,
                                                     /* Pin is configured as I3C1_SCL */
                                                     .mux = kPORT_MuxAlt10,
                                                     /* Digital input enabled */
                                                     .inputBuffer = kPORT_InputBufferEnable,
                                                     /* Digital input is not inverted */
                                                     .invertInput = kPORT_InputNormal,
                                                     /* Pin Control Register fields [15:0] are not locked */
                                                     .lockRegister = kPORT_UnlockRegister};
    /* PORT1_17 (pin F4) is configured as I3C1_SCL */
    PORT_SetPinConfig(PORT1, 17U, &port1_17_pinF4_config);
}

status_t ThermalSensor_Init(void)
{
    status_t result;
    i3c_master_config_t masterConfig;
    p3t1755_config_t p3t1755Config;

    Thermal_InitSensorPins();
    CLOCK_AttachClk(kPLL0_to_I3C1FCLK);
    CLOCK_SetClkDiv(kCLOCK_DivI3c1FClk, 6U);
    SYSCON->CLOCK_CTRL |= SYSCON_CLOCK_CTRL_FRO1MHZ_CLK_ENA_MASK;
    CLOCK_AttachClk(kCLK_1M_to_I3C1FCLKS);
    CLOCK_SetClkDiv(kCLOCK_DivI3c1FClkS, 1U);
    NVIC_SetPriority(I3C1_IRQn, 3U);
    I3C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Hz.i2cBaud          = EXAMPLE_I2C_BAUDRATE;
    masterConfig.baudRate_Hz.i3cPushPullBaud  = EXAMPLE_I3C_PP_BAUDRATE;
    masterConfig.baudRate_Hz.i3cOpenDrainBaud = EXAMPLE_I3C_OD_BAUDRATE;
    masterConfig.enableOpenDrainStop          = false;
    masterConfig.disableTimeout               = true;
    I3C_MasterInit(EXAMPLE_MASTER, &masterConfig, I3C_MASTER_CLOCK_FREQUENCY);
    I3C_MasterTransferCreateHandle(EXAMPLE_MASTER, &g_i3c_m_handle, &masterCallback, NULL);

    result = p3t1755_set_dynamic_address();
    if (result != kStatus_Success)
    {
        (void)PRINTF("\r\nP3T1755 set dynamic address failed.\r\n");
        return result;
    }

    p3t1755Config.writeTransfer = I3C_WriteSensor;
    p3t1755Config.readTransfer  = I3C_ReadSensor;
    p3t1755Config.sensorAddress = SENSOR_ADDR;
    p3t1755Config.oneshotMode   = false;
    result                      = P3T1755_Init(&p3t1755Handle, &p3t1755Config);
    if (result != kStatus_Success)
    {
        (void)PRINTF("\r\nP3T1755 init failed.\r\n");
        return result;
    }

    return kStatus_Success;
}

status_t ThermalSensor_Read(double *temperature)
{
    return P3T1755_ReadTemperature(&p3t1755Handle, temperature);
}
