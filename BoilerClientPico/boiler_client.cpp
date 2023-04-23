/*
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2023 Mykhailo Makarov
 */

#ifndef NDEBUG
#define D(x) x
#else
#define D(x)
#endif

#include <string.h>
#include <time.h>
#include <map>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"

#include "one_wire.h"

#include "hardware/structs/scb.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"

#define TLS_CLIENT_TIMEOUT_SECS 15
#define TLS_CLIENT_PORT 443
#define TLS_CLIENT_REQUEST_PRLG "POST /bm/api/BoilerData HTTP/1.1\r\n" \
                                "Host: " TLS_CLIENT_SERVER "\r\n"      \
                                "Connection: close\r\n"                \
                                "Content-Type: application/json\r\n"   \
                                "Content-Length: "
#define DNS_CACHE_TIMEOUT_MIN 10
#define DNS_CACHE_TIMEOUT_US DNS_CACHE_TIMEOUT_MIN * 60 * 1000000

#define ONEWIRE_GPIO_PIN 22
#define ONEWIRE_NUM_DEV 3
#define ONEWIRE_MIN_TEMP -20
#define ONEWIRE_MAX_TEMP 100

const int PinRed = 12, PinBlue1 = 13, PinGreen = 14, PinBlue2 = 15;

#pragma region Utils

enum colors_t
{
    Off = 0b0000,
    Red = 0b0001,
    Green = 0b0010,
    Yellow = 0b0011,
    Blue = 0b1100,
    Magenta = 0b1101,
    White = 0b1110,
    Builtin = -1
};

static void led_init()
{
    gpio_init(PinRed);
    gpio_init(PinBlue1);
    gpio_init(PinBlue2);
    gpio_init(PinGreen);
    gpio_set_dir(PinRed, true);
    gpio_set_dir(PinBlue1, true);
    gpio_set_dir(PinBlue2, true);
    gpio_set_dir(PinGreen, true);
}

void led_set_color(colors_t i)
{
    gpio_put(PinRed, i & 0b0001);
    gpio_put(PinGreen, i & 0b0010);
    gpio_put(PinBlue1, i & 0b0100);
    gpio_put(PinBlue2, i & 0b1000);
}

static void led_pulse(int count = 1, int delayMs = 250, bool dropLastDelay = false, colors_t color = Builtin)
{
    for (int i = 0; i < count; i++)
    {
        if (color == Builtin)
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        else
            led_set_color(color);
        sleep_ms(delayMs);
        if (color == Builtin)
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        else
            led_set_color(Off);
        if (!dropLastDelay || i < count - 1)
        {
            sleep_ms(delayMs);
        }
    }
}

static bool wifi_init_and_connect()
{
    auto err = cyw43_arch_init();
    if (err)
    {
        printf("failed to initialise, error %d\n", err);
        return false;
    }
    led_pulse(2);

    cyw43_arch_enable_sta_mode();

    err = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000);
    if (err)
    {
        printf("failed to connect, error %d\n", err);
        return false;
    }
    printf("Wifi is UP and JOINed!\n");
    return true;
}

static void sys_set_low_power()
{
    // Low-power sleep(?) TODO: check if has any impact on __wfe()!
    auto save = scb_hw->scr;
    scb_hw->scr = save | M0PLUS_SCR_SLEEPDEEP_BITS;
    printf("Power: deepsleep set!\n");

    // Underclock CPU
    set_sys_clock_khz(68000, true);
    printf("Power: CPU underclocked!\n");

    // Undervolt CPU
    vreg_set_voltage(VREG_VOLTAGE_1_00);
    printf("Power: CPU undervolted!\n");

    // aggressive-low power WiFi state
    cyw43_wifi_pm(&cyw43_state, CYW43_AGGRESSIVE_PM);
    printf("Power: WiFi aggressive power management set!\n");
}

static void reboot()
{
    watchdog_reboot(0, 0, 1000);
}

#pragma endregion

#pragma region TLS Client

typedef struct TLS_CLIENT_T_
{
    struct altcp_pcb *pcb;
    bool complete;   // The web iteration is complete; free resources and cycle
    bool webSuccess; // Response stream has been received from server
    int httpStatus;  // HTTP status returned in the response
    int stage;
    /*1 - Failed to create altcp connection
    **2 - DNS resolution error
    **3 - Failed to connect to server or connection was immediately reset
    **4 - Failed to send data to server
    **5 - SUCCESS (if the rest of fields are OK) - or connection closed without any response
    **6 - Network error during sending or receiving the data
    **7 - Timeout (15 sec) expired
    **8 - Connection was not closed successfully upon cleanup; force-abort utilized */
} TLS_CLIENT_T;

static std::map<uint64_t, float> readings;

static ip_addr_t ipCache;
static uint64_t ipCacheTimestamp;

static err_t tls_client_close(void *arg)
{
    D(printf("tls_client_close()\r\n");)
    TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
    err_t err = ERR_OK;

    state->complete = true;
    if (state->pcb != NULL)
    {
        altcp_arg(state->pcb, NULL);
        altcp_poll(state->pcb, NULL, 0);
        altcp_recv(state->pcb, NULL);
        altcp_err(state->pcb, NULL);
        err = altcp_close(state->pcb);
        if (err != ERR_OK)
        {
            state->stage = 8;
            printf("close failed %d, calling abort\n", err);
            altcp_abort(state->pcb);
            err = ERR_ABRT;
        }
        state->pcb = NULL;
    }
    return err;
}

static err_t tls_client_connected(void *arg, struct altcp_pcb *pcb, err_t err)
{
    D(printf("tls_client_connected()\r\n");)
    TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
    if (err != ERR_OK)
    {
        printf("connect failed %d\n", err);
        return tls_client_close(state);
    }

    D(printf("formatting request...\r\n");)
    state->stage = 4;
    char request[1024], payload[256];
    char *payloadBuffer = payload;
    bool first = true;
    for (auto kvp = readings.begin(); kvp != readings.end(); kvp++)
    {
        payloadBuffer += sprintf(payloadBuffer, "%s\"%016llX\":%.2f", first ? "{" : ",", kvp->first, kvp->second);
        first = false;
    }
    *payloadBuffer = '}';
    payloadBuffer++;
    *payloadBuffer = '\0';
    auto payloadLen = payloadBuffer - payload;
    D(printf("Payload:%s\r\n\tlength: %i\r\n", payload, payloadLen);)
    auto len = sprintf(request, "%s%i\r\n\r\n%s", TLS_CLIENT_REQUEST_PRLG, payloadLen, payload);
    D(printf("Request: length: %i\r\n%s\r\n", len, request);)
    D(printf("connected to server, sending request\n");)
    err = altcp_write(state->pcb, request, len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK)
    {
        printf("error writing data, err=%d", err);
        return tls_client_close(state);
    }

    return ERR_OK;
}

static err_t tls_client_poll(void *arg, struct altcp_pcb *pcb)
{
    TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
    state->stage = 7;
    D(printf("tls_client_poll(): timed out");)
    return tls_client_close(arg);
}

static void tls_client_err(void *arg, err_t err)
{
    TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
    state->stage = MAX(6, state->stage);
    D(printf("tls_client_err(): error %d\n", err);)
    state->pcb = NULL; /* pcb freed by lwip when _err function is called */
    state->complete = true;
}

static err_t tls_client_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    D(printf("tls_client_recv()\r\n");)
    TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
    state->stage = 5;
    if (!p)
    {
        D(printf("connection closed\n");)
        return tls_client_close(state);
    }

    if (p->len > 0)
    {
        state->webSuccess = true;
        char buf[p->len + 1];
        pbuf_copy_partial(p, buf, p->len, 0);
        buf[p->len] = 0;
        D(printf("Data received. Len=%d:\r\n%s\r\n", p->len, buf);)

        if (p->len > 11 && !memcmp("HTTP/1.1", buf, 8))
        {
            state->httpStatus = 100 * (buf[9] - '0') + 10 * (buf[10] - '0') + buf[11] - '0';
            D(printf("Identified web status as HTTP %d\r\n", state->httpStatus);)
        }

        altcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);

    return ERR_OK;
}

static void tls_client_connect_to_server_ip(TLS_CLIENT_T *state)
{
    D(printf("tls_client_connect_to_server_ip(): connecting to server IP %s port %d\n", ipaddr_ntoa(&ipCache), TLS_CLIENT_PORT);)
    state->stage = 3;
    err_t err = altcp_connect(state->pcb, &ipCache, TLS_CLIENT_PORT, tls_client_connected);
    if (err != ERR_OK)
    {
        fprintf(stderr, "error initiating connect, err=%d\n", err);
        tls_client_close(state);
    }
}

static void tls_client_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
    D(printf("tls_client_dns_found()\r\n");)
    if (ipaddr && ipaddr->addr)
    {
        D(printf("DNS resolving complete\n");)
        ipCache = *ipaddr;
        tls_client_connect_to_server_ip((TLS_CLIENT_T *)arg);
    }
    else
    {
        printf("error resolving hostname %s\n", hostname);
        tls_client_close(arg);
    }
}

static bool sendPacket(const char *hostname, TLS_CLIENT_T *state, altcp_tls_config *tls_config)
{
    state->pcb = altcp_tls_new(tls_config, IPADDR_TYPE_ANY);
    state->stage = 1;
    if (!state->pcb)
    {
        printf("failed to create pcb\n");
        return false;
    }

    altcp_arg(state->pcb, state);
    altcp_poll(state->pcb, tls_client_poll, TLS_CLIENT_TIMEOUT_SECS * 2);
    altcp_recv(state->pcb, tls_client_recv);
    altcp_err(state->pcb, tls_client_err);

    mbedtls_ssl_set_hostname((mbedtls_ssl_context *)altcp_tls_context(state->pcb), hostname);

    cyw43_arch_lwip_begin();

    auto now = time_us_64();
    err_t err;
    if (ipCache.addr && now - ipCacheTimestamp < DNS_CACHE_TIMEOUT_US)
    {
        // host is in local 5 min DNS cache
        D(printf("Using cached IP %s for hostname %s at %.2fs\n", ipaddr_ntoa(&ipCache), hostname, now / 1000000.0);)
        tls_client_connect_to_server_ip(state);
        err = ERR_OK;
    }
    else
    {
        ipCacheTimestamp = now;
        D(printf("Resolving %s at %.2fs\n", hostname, now / 1000000.0);)
        state->stage = 2;
        err = dns_gethostbyname(hostname, &ipCache, tls_client_dns_found, state);
        if (err == ERR_OK)
        {
            // host is in LWIP DNS cache (which does not seem to ever hit)
            tls_client_connect_to_server_ip(state);
        }
        else if (err != ERR_INPROGRESS)
        {
            printf("error initiating DNS resolving, err=%d\n", err);
            tls_client_close(state->pcb);
        }
    }

    cyw43_arch_lwip_end();

    return err == ERR_OK || err == ERR_INPROGRESS;
}

#pragma endregion

TLS_CLIENT_T *sendPacketIteration(altcp_tls_config *tls_config)
{
    auto state = (TLS_CLIENT_T *)calloc(1, sizeof(TLS_CLIENT_T));
    if (!state)
    {
        return nullptr;
    }
    if (!sendPacket(TLS_CLIENT_SERVER, state, tls_config))
    {
        return state;
    }
    while (!state->complete)
    {
        led_pulse(1, 100);
    }
    return state;
}

bool oneWireIteration(One_wire *sensor)
{
    D(printf("Searching...\r\n");)
    int count = sensor->find_and_count_devices_on_bus();
    D(printf("Found %i devices\r\n", count);)
    if (count != ONEWIRE_NUM_DEV)
    {
        return false;
    }
    rom_address_t null_address{};
    sensor->convert_temperature(null_address, true, true);
    for (int i = 0; i < MIN(count, 3); i++)
    {
        auto address = One_wire::get_address(i);
        auto temp = sensor->temperature(address);
        auto addr64 = One_wire::to_uint64(address);
        D(printf("%016llX\t%3.1f*C\r\n", addr64, temp);)
        if (temp < ONEWIRE_MIN_TEMP || temp > ONEWIRE_MAX_TEMP)
        {
            return false;
        }
        readings[addr64] = temp;
    }
    return true;
}

void primaryCycle()
{
    /* No CA certificate checking */
    D(printf("Init web config...\r\n");)
    auto tls_config = altcp_tls_create_config_client(NULL, 0);

    D(printf("Init OneWire...\r\n");)
    One_wire one_wire(ONEWIRE_GPIO_PIN);
    one_wire.init();

    // Fully initialized here
    led_set_color(Off);

    int cycles = 1;
    int consecutiveErrors = 0;
    while (true)
    {
        if (--cycles > 0)
            goto skip;
        cycles = 1;
        if (oneWireIteration(&one_wire))
        {
            auto state = sendPacketIteration(tls_config);
            if (state)
            {
                D(printf("Iteration: web success:%s, HTTP status code: %i, sequence stage: %i.\r\n", state->webSuccess ? "yes" : "no", state->httpStatus, state->stage);)
                if (state->webSuccess && state->httpStatus == 200)
                {
                    led_pulse(1, 1000, true, Green);
                    cycles = 12;
                    consecutiveErrors = 0;
                }
                else
                {
                    led_pulse(state->stage, 200, true, Red);
                    if (++consecutiveErrors >= 5)
                    {
                        cyw43_arch_deinit();
                        reboot();
                        goto skip; // drop to __wfe() until watchdog's cycle clicks.
                    }
                }
                free(state);
            }
            else
            {
                led_pulse(1, 1000, true, Yellow);
                D(printf("Iteration: unable to create State.\r\n");)
            }
        }
        else
        {
            led_pulse(1, 1000, true, Blue);
            D(printf("Iteration: error during sensor polling.\r\n");)
        }
    skip:
        sleep_us(5000000 - (time_us_32() % 1000000));
    }

    altcp_tls_free_config(tls_config);
}

int main()
{
    stdio_init_all();

    led_init();
    led_set_color(White);

    if (!wifi_init_and_connect())
    {
        led_pulse(1, 2000, true, Magenta);
        reboot();
        return 1;
    }
    led_pulse(3);

    sys_set_low_power();

    primaryCycle();

    /* sleep a bit to let usb stdio write out any buffer to host */
    sleep_ms(100);

    cyw43_arch_deinit();
    return 0;
}
