/*
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2023 Mykhailo Makarov
 */

#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include <map>

#include "one_wire.h"

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

void setLedColor(colors_t i)
{
    gpio_put(PinRed, i & 0b0001);
    gpio_put(PinGreen, i & 0b0010);
    gpio_put(PinBlue1, i & 0b0100);
    gpio_put(PinBlue2, i & 0b1000);
}

static void pulse(int count = 1, int delayMs = 250, bool dropLastDelay = false, colors_t color = Builtin)
{
    for (int i = 0; i < count; i++)
    {
        if (color == Builtin)
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        else
            setLedColor(color);
        sleep_ms(delayMs);
        if (color == Builtin)
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        else
            setLedColor(Off);
        if (!dropLastDelay || i < count - 1)
        {
            sleep_ms(delayMs);
        }
    }
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
    /*1 - Failed to created altcp connection
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
    printf("tls_client_close()\r\n");
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
    printf("tls_client_connected()\r\n");
    TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
    if (err != ERR_OK)
    {
        printf("connect failed %d\n", err);
        return tls_client_close(state);
    }

    printf("formatting request...\r\n");
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
    printf("Payload:%s\r\n\tlength: %i\r\n", payload, payloadLen);
    auto len = sprintf(request, "%s%i\r\n\r\n%s", TLS_CLIENT_REQUEST_PRLG, payloadLen, payload);
    printf("Request: length: %i\r\n%s\r\n", len, request);

    printf("connected to server, sending request\n");
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
    printf("tls_client_poll(): timed out");
    return tls_client_close(arg);
}

static void tls_client_err(void *arg, err_t err)
{
    TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
    state->stage = MAX(6, state->stage);
    printf("tls_client_err(): error %d\n", err);
    state->pcb = NULL; /* pcb freed by lwip when _err function is called */
    state->complete = true;
}

static err_t tls_client_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    printf("tls_client_recv()\r\n");
    TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
    state->stage = 5;
    if (!p)
    {
        printf("connection closed\n");
        return tls_client_close(state);
    }

    if (p->len > 0)
    {
        state->webSuccess = true;
        /* For simplicity this examples creates a buffer on stack the size of the data pending here,
           and copies all the data to it in one go.
           Do be aware that the amount of data can potentially be a bit large (TLS record size can be 16 KB),
           so you may want to use a smaller fixed size buffer and copy the data to it using a loop, if memory is a concern */
        char buf[p->len + 1];
        pbuf_copy_partial(p, buf, p->len, 0);
        buf[p->tot_len] = 0;
        printf("Data received. Len=%d:\r\n%s\r\n", p->len, buf);

        if (p->len > 11 && !memcmp("HTTP/1.1", buf, 8))
        {
            state->httpStatus = 100 * (buf[9] - '0') + 10 * (buf[10] - '0') + buf[11] - '0';
            printf("Identified web status as HTTP %d\r\n", state->httpStatus);
        }

        altcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);

    return ERR_OK;
}

static void tls_client_connect_to_server_ip(TLS_CLIENT_T *state)
{
    printf("tls_client_connect_to_server_ip(): connecting to server IP %s port %d\n", ipaddr_ntoa(&ipCache), TLS_CLIENT_PORT);
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
    printf("tls_client_dns_found()\r\n");
    if (ipaddr && ipaddr->addr)
    {
        printf("DNS resolving complete\n");
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

    /* Set SNI */
    mbedtls_ssl_set_hostname((mbedtls_ssl_context *)altcp_tls_context(state->pcb), hostname);

    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    cyw43_arch_lwip_begin();

    auto now = time_us_64();
    err_t err;
    if (ipCache.addr && now - ipCacheTimestamp < DNS_CACHE_TIMEOUT_US)
    {
        // host is in local 5 min DNS cache
        printf("Using cached IP %s for hostname %s at %.2fs\n", ipaddr_ntoa(&ipCache), hostname, now / 1000000.0);
        tls_client_connect_to_server_ip(state);
        err = ERR_OK;
    }
    else
    {
        ipCacheTimestamp = now;
        printf("Resolving %s at %.2fs\n", hostname, now / 1000000.0);
        state->stage = 2;
        err = dns_gethostbyname(hostname, &ipCache, tls_client_dns_found, state);
        if (err == ERR_OK)
        {
            /* host is in LWIP DNS cache (which does not seem to ever hit) */
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
    // Perform initialisation
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
        pulse(1, 100);
    }
    return state;
}

void run_tls_client_test()
{
    /* No CA certificate checking */
    auto tls_config = altcp_tls_create_config_client(NULL, 0);

    while (true)
    {
        auto s = sendPacketIteration(tls_config);
        if (s)
            free(s);
        sleep_ms(5000);
    }

    altcp_tls_free_config(tls_config);
}

bool oneWireIteration(One_wire *sensor)
{
    printf("Searching...\r\n");
    int count = sensor->find_and_count_devices_on_bus();
    printf("Found %i devices\r\n", count);
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
        printf("%016llX\t%3.1f*C\r\n", addr64, temp);
        if (temp < ONEWIRE_MIN_TEMP || temp > ONEWIRE_MAX_TEMP)
        {
            return false;
        }
        readings[addr64] = temp;
    }
    return true;
}

void run_onewire_test()
{
    printf("Init OneWire...\r\n");
    One_wire one_wire(ONEWIRE_GPIO_PIN);
    one_wire.init();
    sleep_ms(100);
    while (true)
    {
        oneWireIteration(&one_wire);
        sleep_ms(5000);
    }
}

void primaryCycle()
{
    /* No CA certificate checking */
    printf("Init web config...\r\n");
    auto tls_config = altcp_tls_create_config_client(NULL, 0);

    printf("Init OneWire...\r\n");
    One_wire one_wire(ONEWIRE_GPIO_PIN);
    one_wire.init();

    setLedColor(Off);
    int cycles = 1;
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
                printf("Iteration: web success:%s, HTTP status code: %i, sequence stage: %i.\r\n", state->webSuccess ? "yes" : "no", state->httpStatus, state->stage);
                if (state->webSuccess && state->httpStatus == 200)
                {
                    pulse(1, 1000, true, Green);
                    cycles = 12;
                }
                else
                {
                    pulse(state->stage, 200, true, Red);
                }
                free(state);
            }
            else
            {
                pulse(1, 1000, true, Yellow);
                printf("Iteration: unable to create State.\r\n");
            }
        }
        else
        {
            pulse(1, 1000, true, Blue);
            printf("Iteration: error during sensor polling.\r\n");
        }
    skip:
        sleep_us(4000000 + (1000000 - (time_us_32() % 1000000)));
    }

    altcp_tls_free_config(tls_config);
}

void test_led()
{
    colors_t colors[] = {Off, Red, Green, Yellow, Blue, Magenta, White};
    while (1)
    {
        for (auto &&c : colors)
        {
            setLedColor(c);
            sleep_ms(5000);
        }
    }
}

int main()
{
    stdio_init_all();

    gpio_init(PinRed);
    gpio_init(PinBlue1);
    gpio_init(PinBlue2);
    gpio_init(PinGreen);
    gpio_set_dir(PinRed, true);
    gpio_set_dir(PinBlue1, true);
    gpio_set_dir(PinBlue2, true);
    gpio_set_dir(PinGreen, true);
    // test_led();

    setLedColor(White);

    if (cyw43_arch_init())
    {
        printf("failed to initialise\n");
        return 1;
    }
    pulse(2);

    // run_onewire_test();

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000))
    {
        printf("failed to connect\n");
        return 1;
    }
    pulse(3);

    // run_tls_client_test();
    primaryCycle();

    /* sleep a bit to let usb stdio write out any buffer to host */
    sleep_ms(100);

    cyw43_arch_deinit();
    return 0;
}
