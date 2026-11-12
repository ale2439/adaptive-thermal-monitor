/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2022, 2025 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include "mqtt_freertos.h"
#include "thermal_sensor.h"
#include "thermal_logic.h"
#include "thermal_buffer.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "board.h"
#include "fsl_silicon_id.h"

#include "lwip/opt.h"
#include "lwip/api.h"
#include "lwip/apps/mqtt.h"
#include "lwip/tcpip.h"
#if MQTT_OUTPUT_RINGBUF_SIZE < 512
#error "Recovery MQTT payload requires MQTT_OUTPUT_RINGBUF_SIZE >= 512"
#endif


/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*! @brief MQTT server host name or IP address. */
#ifndef EXAMPLE_MQTT_SERVER_HOST
#define EXAMPLE_MQTT_SERVER_HOST "10.0.0.249"
#endif

/*! @brief MQTT server port number. */
#ifndef EXAMPLE_MQTT_SERVER_PORT
#define EXAMPLE_MQTT_SERVER_PORT 1883
#endif

/*! @brief Stack size of the temporary lwIP initialization thread. */
#define INIT_THREAD_STACKSIZE 1024

/*! @brief Priority of the temporary lwIP initialization thread. */
#define INIT_THREAD_PRIO DEFAULT_THREAD_PRIO

/*! @brief Stack size of the temporary initialization thread. */
#define APP_THREAD_STACKSIZE 2048

/*! @brief Priority of the temporary initialization thread. */
#define APP_THREAD_PRIO DEFAULT_THREAD_PRIO

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

static void connect_to_mqtt(void *ctx);

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*! @brief MQTT client data. */
static mqtt_client_t *mqtt_client;

/*! @brief MQTT client ID string. */
static char client_id[(SILICONID_MAX_LENGTH * 2) + 5];

/*! @brief MQTT client information. */
static const struct mqtt_connect_client_info_t mqtt_client_info = {
    .client_id   = (const char *)&client_id[0],
    .client_user = NULL,
    .client_pass = NULL,
    .keep_alive  = 10,
    .will_topic  = NULL,
    .will_msg    = NULL,
    .will_qos    = 0,
    .will_retain = 0,
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    .tls_config = NULL,
#endif
};

/*! @brief MQTT broker IP address. */
static ip_addr_t mqtt_addr;

/*! @brief Indicates connection to MQTT broker. */
static volatile bool connected = false;

/*******************************************************************************
 * Code
 ******************************************************************************/

#define THERMAL_TOPIC "thermal/board1/readings"
#define SESSION_TOPIC "thermal/board1/session"
#define ACK_TOPIC "thermal/board1/ack"
#define HELLO_TOPIC "thermal/board1/hello"
typedef struct { unsigned kind; uint32_t seq; char session[33]; } thermal_event_t;
static QueueHandle_t events;
static char rx[80];
static unsigned rxKind, rxLength;
static void receive_header(void *arg, const char *topic, u32_t length)
{
    (void)arg;
    rxLength = 0;
    rxKind = length < sizeof(rx) ?
        (!strcmp(topic, SESSION_TOPIC) ? 1U : !strcmp(topic, ACK_TOPIC) ? 2U : 0U) : 0U;
}
static void receive_data(void *arg, const u8_t *data, u16_t length, u8_t flags)
{
    (void)arg;
    if (!rxKind || rxLength + length >= sizeof(rx)) { rxKind = 0; return; }
    memcpy(rx + rxLength, data, length); rxLength += length;
    if (!(flags & MQTT_DATA_FLAG_LAST)) return;
    rx[rxLength] = 0;
    thermal_event_t e = {0}; e.kind = rxKind;
    if (rxLength < 32) return;
    for (unsigned i = 0; i < 32; i++) if (!isxdigit((unsigned char)rx[i])) return;
    memcpy(e.session, rx, 32); e.session[32] = 0;
    if (rxKind == 1 && rxLength != 32) return;
    if (rxKind == 2) {
        if (rxLength < 34 || rx[32] != ':') return;
        uint64_t seq = 0;
        for (unsigned i = 33; i < rxLength; i++) {
            if (rx[i] < '0' || rx[i] > '9') return;
            seq = seq * 10U + (unsigned)(rx[i] - '0');
            if (seq > UINT32_MAX) return;
        }
        e.seq = (uint32_t)seq;
    }
    (void)xQueueSend(events, &e, 0); /* Overflow is safe: timeout retries. */
}

/*!
 * @brief Called when connection state changes.
 */
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    const struct mqtt_connect_client_info_t *client_info = (const struct mqtt_connect_client_info_t *)arg;

    connected = (status == MQTT_CONNECT_ACCEPTED);

    switch (status)
    {
        case MQTT_CONNECT_ACCEPTED:
            PRINTF("MQTT client \"%s\" connected.\r\n", client_info->client_id);
            mqtt_set_inpub_callback(client, receive_header, receive_data, NULL);
            (void)mqtt_subscribe(client, SESSION_TOPIC, 1, NULL, NULL);
            (void)mqtt_subscribe(client, ACK_TOPIC, 1, NULL, NULL);
            break;

        case MQTT_CONNECT_DISCONNECTED:
            PRINTF("MQTT client \"%s\" not connected.\r\n", client_info->client_id);
            /* Try to reconnect 1 second later */
            sys_timeout(1000, connect_to_mqtt, NULL);
            break;

        case MQTT_CONNECT_TIMEOUT:
            PRINTF("MQTT client \"%s\" connection timeout.\r\n", client_info->client_id);
            /* Try again 1 second later */
            sys_timeout(1000, connect_to_mqtt, NULL);
            break;

        case MQTT_CONNECT_REFUSED_PROTOCOL_VERSION:
        case MQTT_CONNECT_REFUSED_IDENTIFIER:
        case MQTT_CONNECT_REFUSED_SERVER:
        case MQTT_CONNECT_REFUSED_USERNAME_PASS:
        case MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_:
            PRINTF("MQTT client \"%s\" connection refused: %d.\r\n", client_info->client_id, (int)status);
            /* Try again 10 seconds later */
            sys_timeout(10000, connect_to_mqtt, NULL);
            break;

        default:
            PRINTF("MQTT client \"%s\" connection status: %d.\r\n", client_info->client_id, (int)status);
            /* Try again 10 seconds later */
            sys_timeout(10000, connect_to_mqtt, NULL);
            break;
    }
}

/*!
 * @brief Starts connecting to MQTT broker. To be called on tcpip_thread.
 */
static void connect_to_mqtt(void *ctx)
{
    LWIP_UNUSED_ARG(ctx);

    PRINTF("Connecting to MQTT broker at %s...\r\n", ipaddr_ntoa(&mqtt_addr));

    sys_untimeout(connect_to_mqtt, NULL);
    err_t result = mqtt_client_connect(mqtt_client, &mqtt_addr, EXAMPLE_MQTT_SERVER_PORT,
        mqtt_connection_cb, LWIP_CONST_CAST(void *, &mqtt_client_info), &mqtt_client_info);
    if (result != ERR_OK)
    {
        connected = false;
        PRINTF("Connection could not start: %d; retrying.\r\n", result);
        sys_timeout(2000U, connect_to_mqtt, NULL);
    }
}

static void hello(void *ctx)
{
    (void)ctx;
    if (connected) (void)mqtt_publish(mqtt_client, HELLO_TOPIC, "start", 5, 0, 0, NULL, NULL);
}
typedef struct { char payload[384]; } thermal_message_t;
static void publish_message(void *ctx)
{
    thermal_message_t *message = ctx;
    if (connected) {
        err_t result = mqtt_publish(mqtt_client, THERMAL_TOPIC, message->payload,
            (u16_t)strlen(message->payload), 1, 0, NULL, NULL);
        if (result != ERR_OK) PRINTF("MQTT queue failed: %d; retained for retry.\r\n", result);
    }
    vPortFree(message);
}
/* Only the app task touches the ring or its in-flight state. */
static thermal_buffer_t buffer;
static bool queue_sample(thermal_sample_t s, const char *session, bool recovering)
{
    thermal_message_t *m = pvPortMalloc(sizeof(*m));
    if (!m) return false;
    uint32_t magnitude = s.temperature_mc < 0 ? (uint32_t)-s.temperature_mc : (uint32_t)s.temperature_mc;
    uint32_t age = (uint32_t)(sys_now() - s.sampled_ms);
    int n = snprintf(m->payload, sizeof(m->payload),
        "{\"device_id\":\"board1\",\"session_id\":\"%s\",\"seq\":%lu,\"uptime_ms\":%lu,"
        "\"temperature_c\":%s%lu.%03lu,\"alert\":%s,\"max_interval_ms\":10000,"
        "\"reporting_mode\":\"adaptive\",\"sample_age_ms\":%lu,\"recovered\":%s,"
        "\"buffer_count\":%lu,\"dropped_samples\":%lu}",
        session, (unsigned long)s.seq, (unsigned long)s.sampled_ms,
        s.temperature_mc < 0 ? "-" : "", (unsigned long)(magnitude / 1000),
        (unsigned long)(magnitude % 1000), s.alert ? "true" : "false",
        (unsigned long)age, recovering ? "true" : "false",
        (unsigned long)buffer.count, (unsigned long)buffer.dropped);
    if (n < 0 || (size_t)n >= sizeof(m->payload) || tcpip_callback(publish_message, m) != ERR_OK) {
        vPortFree(m); return false;
    }
    return true;
}

/*!
 * @brief Application thread.
 */
static void app_thread(void *arg)
{
    struct netif *netif = (struct netif *)arg;
    err_t err;
    bool sensorReady = false;
    bool alertActive = false;
    uint32_t sequence = 0U;


    PRINTF("\r\nIPv4 Address     : %s\r\n", ipaddr_ntoa(&netif->ip_addr));
    PRINTF("IPv4 Subnet mask : %s\r\n", ipaddr_ntoa(&netif->netmask));
    PRINTF("IPv4 Gateway     : %s\r\n\r\n", ipaddr_ntoa(&netif->gw));

    /*
     * Check if we have an IP address or host name string configured.
     * Could just call netconn_gethostbyname() on both IP address or host name,
     * but we want to print some info if goint to resolve it.
     */
    if (ipaddr_aton(EXAMPLE_MQTT_SERVER_HOST, &mqtt_addr) && IP_IS_V4(&mqtt_addr))
    {
        /* Already an IP address */
        err = ERR_OK;
    }
    else
    {
        /* Resolve MQTT broker's host name to an IP address */
        PRINTF("Resolving \"%s\"...\r\n", EXAMPLE_MQTT_SERVER_HOST);
        err = netconn_gethostbyname(EXAMPLE_MQTT_SERVER_HOST, &mqtt_addr);
    }

    if (err == ERR_OK)
    {
        /* Start connecting to MQTT broker from tcpip_thread */
        err = tcpip_callback(connect_to_mqtt, NULL);
        if (err != ERR_OK)
        {
            PRINTF("Failed to invoke broker connection on the tcpip_thread: %d.\r\n", err);
        }
    }
    else
    {
        PRINTF("Failed to obtain IP address: %d.\r\n", err);
    }

    /* All samples are kept until a covering adaptive report is acknowledged.
       Once a report fails, chronological replay preserves the buffered interval. */
    char session[33] = {0};
    bool inFlight = false, recovering = false, reported = false;
    bool lastAlert = false;
    int32_t lastMc = 0;
    uint32_t pendingSeq = 0, pendingSince = 0, lastMs = 0;
    uint32_t nextSample = sys_now(), lastHello = sys_now() - 2000U;
    while (1)
    {
        uint32_t now = sys_now();
        thermal_event_t e;
        while (xQueueReceive(events, &e, 0) == pdTRUE) {
            if (e.kind == 1 && !session[0]) {
                memcpy(session, e.session, sizeof(session));
                PRINTF("Dashboard session assigned: %s\r\n", session);
            }
            if (e.kind == 2 && inFlight && !strcmp(session, e.session) && e.seq == pendingSeq) {
                ThermalBuffer_Acknowledge(&buffer, e.seq);
                inFlight = false;
                if (recovering) PRINTF("Saved sample %lu; backlog %lu, dropped %lu\r\n",
                    (unsigned long)e.seq, (unsigned long)buffer.count, (unsigned long)buffer.dropped);
                if (!buffer.count) recovering = false;
            }
        }
        if (!connected || (inFlight && (uint32_t)(now - pendingSince) >= 2000U)) {
            if (inFlight || buffer.count) recovering = true;
            inFlight = false;
        }
        if (!session[0] && buffer.count &&
            (uint32_t)(now - buffer.samples[buffer.head].sampled_ms) >= 2000U) recovering = true;
        if (connected && !session[0] && (uint32_t)(now - lastHello) >= 2000U) {
            (void)tcpip_callback(hello, NULL); lastHello = now;
        }
        if ((int32_t)(now - nextSample) >= 0) {
            nextSample = now + 1000U;
            if (!sensorReady) {
                sensorReady = ThermalSensor_Init() == kStatus_Success;
                if (!sensorReady) { PRINTF("Sensor initialization failed; retrying.\r\n"); }
                else PRINTF("P3T1755 ready. Alert ON >=28.000 C, OFF <=27.800 C.\r\n");
            }
            double temperature;
            if (sensorReady && ThermalSensor_Read(&temperature) == kStatus_Success) {
                int32_t mc = (int32_t)(temperature * 1000.0 + (temperature >= 0 ? .5 : -.5));
                alertActive = Thermal_UpdateAlert(alertActive, mc);
                thermal_sample_t s = { ++sequence, sys_now(), mc, alertActive };
                ThermalBuffer_Push(&buffer, s);
                uint32_t magnitude = mc < 0 ? (uint32_t)-mc : (uint32_t)mc;
                PRINTF("Sample %lu: %s%lu.%03lu C | %s | MQTT %s | buffer %lu | dropped %lu\r\n",
                    (unsigned long)sequence, mc < 0 ? "-" : "",
                    (unsigned long)(magnitude / 1000), (unsigned long)(magnitude % 1000),
                    alertActive ? "ALERT" : "NORMAL", connected ? "connected" : "offline",
                    (unsigned long)buffer.count, (unsigned long)buffer.dropped);
            } else if (sensorReady) {
                PRINTF("Sensor read failed; no invented sample.\r\n"); sensorReady = false;
            }
        }
        if (!inFlight && connected && session[0] && buffer.count) {
            thermal_sample_t s = buffer.samples[recovering ? buffer.head :
                (buffer.head + buffer.count - 1U) % THERMAL_BUFFER_CAPACITY];
            if (recovering || Thermal_ReportDue(reported, sys_now(), lastMs,
                    s.temperature_mc, lastMc, s.alert, lastAlert)) {
                if (queue_sample(s, session, recovering)) {
                    inFlight = true; pendingSeq = s.seq; pendingSince = sys_now();
                    reported = true; lastMs = pendingSince; lastMc = s.temperature_mc; lastAlert = s.alert;
                }
            }
        }
        sys_msleep(50U); /* Replay at up to 20 records/s; sampling remains ~1 Hz. */
    }
}

static void generate_client_id(void)
{
    uint8_t silicon_id[SILICONID_MAX_LENGTH];
    const char *hex = "0123456789abcdef";
    status_t status;
    uint32_t id_len = sizeof(silicon_id);
    int idx         = 0;
    int i;
    bool id_is_zero = true;

    /* Get unique ID of SoC */
    status = SILICONID_GetID(&silicon_id[0], &id_len);
    assert(status == kStatus_Success);
    assert(id_len > 0U);
    (void)status;

    /* Covert unique ID to client ID string in form: nxp_hex-unique-id */

    /* Check if client_id can accomodate prefix, id and terminator */
    assert(sizeof(client_id) >= (5U + (2U * id_len)));

    /* Fill in prefix */
    client_id[idx++] = 'n';
    client_id[idx++] = 'x';
    client_id[idx++] = 'p';
    client_id[idx++] = '_';

    /* Append unique ID */
    for (i = (int)id_len - 1; i >= 0; i--)
    {
        uint8_t value    = silicon_id[i];
        client_id[idx++] = hex[value >> 4];
        client_id[idx++] = hex[value & 0xFU];

        if (value != 0)
        {
            id_is_zero = false;
        }
    }

    /* Terminate string */
    client_id[idx] = '\0';

    if (id_is_zero)
    {
        PRINTF(
            "WARNING: MQTT client id is zero. (%s)"
#ifdef OCOTP
            " This might be caused by blank OTP memory."
#endif
            "\r\n",
            client_id);
    }
}

/*!
 * @brief Create and run example thread
 *
 * @param netif  netif which example should use
 */
void mqtt_freertos_run_thread(struct netif *netif)
{
    LOCK_TCPIP_CORE();
    mqtt_client = mqtt_client_new();
    UNLOCK_TCPIP_CORE();
    if (mqtt_client == NULL)
    {
        PRINTF("mqtt_client_new() failed.\r\n");
        while (1)
        {
        }
    }

    generate_client_id();
    events = xQueueCreate(16U, sizeof(thermal_event_t));
    LWIP_ASSERT("Recovery event queue allocation failed", events != NULL);

    if (sys_thread_new("app_task", app_thread, netif, APP_THREAD_STACKSIZE, APP_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("mqtt_freertos_start_thread(): Task creation failed.", 0);
    }
}
