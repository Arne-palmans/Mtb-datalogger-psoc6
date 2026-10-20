#include "cycfg_pins.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cy_systick.h"
#include "cyhal_gpio.h"
#include <stdbool.h>
#include <stdio.h>

// =====================================================================
//  Constanten
// =====================================================================
#define SAMPLE_INTERVAL_MS  1000
#define ADC_SOURCE          0
#define BUTTON_SOURCE       1
#define HISTORY_SIZE        10
#define SAMPLE_BUFFER_SIZE  32

// =====================================================================
//  FSM – states en events
// =====================================================================
typedef enum {
    ST_IDLE,
    ST_LOGGING,
    ST_DUMPING
} fsm_state_t;

typedef enum {
    EVT_NONE,
    EVT_TIMER_TICK,   // sampling-interval verstreken
    EVT_BTN_PRESS,    // user-button ingedrukt
    EVT_CMD_S,        // UART 's' -> start
    EVT_CMD_P,        // UART 'p' -> pause
    EVT_CMD_D         // UART 'd' -> dump
} fsm_event_t;

// =====================================================================
//  Sample-struct + circulaire buffer
// =====================================================================
typedef struct {
    uint32_t timestamp_ms;
    int16_t  value;
    uint8_t  source_id;
} sample_t;

typedef struct {
    sample_t data[SAMPLE_BUFFER_SIZE];
    uint8_t  head;
    uint8_t  tail;
    uint8_t  count;
} sample_buffer_t;

// =====================================================================
//  Globale variabelen
// =====================================================================
cyhal_adc_t         adc;
cyhal_adc_channel_t adc_chan;

volatile uint32_t g_time_ms  = 0;
volatile char     g_uart_cmd = 0;   // laatste ontvangen UART-teken

int16_t history[HISTORY_SIZE];
uint8_t history_index = 0;
uint8_t history_count = 0;

sample_buffer_t g_buf = { .head = 0, .tail = 0, .count = 0 };

fsm_state_t g_state = ST_IDLE;      // beginstate

// =====================================================================
//  SysTick callback (elke 1 ms)
// =====================================================================
void my_systick_callback(void)
{
    g_time_ms++;
}

// =====================================================================
//  Buffer-functies
// =====================================================================
void buffer_push(sample_buffer_t *buf, const sample_t *s)
{
    buf->data[buf->head] = *s;
    buf->head = (buf->head + 1) % SAMPLE_BUFFER_SIZE;
    if (buf->count < SAMPLE_BUFFER_SIZE)
        buf->count++;
    else
        buf->tail = (buf->tail + 1) % SAMPLE_BUFFER_SIZE;
}

bool buffer_pop(sample_buffer_t *buf, sample_t *out)
{
    if (buf->count == 0) return false;
    *out = buf->data[buf->tail];
    buf->tail = (buf->tail + 1) % SAMPLE_BUFFER_SIZE;
    buf->count--;
    return true;
}

void buffer_flush(sample_buffer_t *buf)
{
    sample_t s;
    printf("--- DUMP START (%u samples) ---\r\n", buf->count);
    while (buffer_pop(buf, &s))
        printf("[%8lu ms] src=%u val=%d\r\n",
               (unsigned long)s.timestamp_ms, s.source_id, s.value);
    printf("--- DUMP END ---\r\n");
}

// =====================================================================
//  Hulpfuncties
// =====================================================================
sample_t make_sample(uint8_t src, int16_t val)
{
    return (sample_t){ .timestamp_ms = g_time_ms,
                       .value        = val,
                       .source_id    = src };
}

int16_t update_min(int16_t new_val)
{
    history[history_index] = new_val;
    history_index = (history_index + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) history_count++;

    int16_t min = history[0];
    for (uint8_t i = 1; i < history_count; i++)
        if (history[i] < min) min = history[i];
    return min;
}

int16_t adc_read(void)
{
    return (int16_t)cyhal_adc_read(&adc_chan);
}

// =====================================================================
//  Event-detectie: lees UART (non-blocking) en button
// =====================================================================
fsm_event_t poll_event(bool *btn_prev, uint32_t *last_tick)
{
    // --- UART (non-blocking) ---
    char c = g_uart_cmd;
    if (c != 0) {
        g_uart_cmd = 0;
        if (c == 's') return EVT_CMD_S;
        if (c == 'p') return EVT_CMD_P;
        if (c == 'd') return EVT_CMD_D;
    }

    // --- Button (falling edge = ingedrukt) ---
    bool btn_now = cyhal_gpio_read(CYBSP_USER_BTN);
    if (!btn_now && *btn_prev) {           // neergaande flank
        *btn_prev = btn_now;
        return EVT_BTN_PRESS;
    }
    *btn_prev = btn_now;

    // --- Timer tick ---
    uint32_t now = g_time_ms;
    if ((now - *last_tick) >= SAMPLE_INTERVAL_MS) {
        *last_tick = now;
        return EVT_TIMER_TICK;
    }

    return EVT_NONE;
}

// =====================================================================
//  FSM-transitie
// =====================================================================
void fsm_handle(fsm_event_t evt)
{
    switch (g_state)
    {
    // -----------------------------------------------------------------
    case ST_IDLE:
        if (evt == EVT_BTN_PRESS || evt == EVT_CMD_S) {
            printf("[FSM] IDLE -> LOGGING\r\n");
            g_state = ST_LOGGING;
        }
        else if (evt == EVT_CMD_D) {
            printf("[FSM] IDLE -> DUMPING\r\n");
            g_state = ST_DUMPING;
        }
        break;

    // -----------------------------------------------------------------
	case ST_LOGGING:
    if (evt == EVT_TIMER_TICK) {
        int16_t  val = adc_read();
        sample_t s   = make_sample(ADC_SOURCE, val);
        buffer_push(&g_buf, &s);
        printf("[LOG] src=%u val=%d  min=%d  buf=%u\r\n",
               s.source_id, s.value,
               update_min(val), g_buf.count);
    }
    else if (evt == EVT_BTN_PRESS) {                    // <-- gewijzigd
        int16_t  val = adc_read();                      // lees ADC op moment van druk
        sample_t s   = make_sample(BUTTON_SOURCE, val); // src=1 i.p.v. 0
        buffer_push(&g_buf, &s);
        printf("[BTN] src=%u val=%d  buf=%u\r\n",
               s.source_id, s.value, g_buf.count);
    }
    else if (evt == EVT_CMD_P) {
        printf("[FSM] LOGGING -> IDLE (pause)\r\n");
        g_state = ST_IDLE;
    }
    else if (evt == EVT_CMD_D) {                        // dump alleen via 'd'
        printf("[FSM] LOGGING -> DUMPING\r\n");
        g_state = ST_DUMPING;
    }
    break;
    case ST_DUMPING:
        // Dump wordt uitgevoerd in main-loop; na afloop terug naar IDLE
        buffer_flush(&g_buf);
        printf("[FSM] DUMPING -> IDLE\r\n");
        g_state = ST_IDLE;
        break;
    }
}

// =====================================================================
//  main
// =====================================================================
int main(void)
{
    cy_rslt_t result;

    result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX,
                                  CYBSP_DEBUG_UART_RX, 115200);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    result = cyhal_adc_init(&adc, P10_0, NULL);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    cyhal_adc_channel_config_t ch_cfg = {
        .enable_averaging   = false,
        .min_acquisition_ns = 1000,
        .enabled            = true
    };
    result = cyhal_adc_channel_init_diff(&adc_chan, &adc,
                 P10_0, CYHAL_ADC_VNEG, &ch_cfg);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    result = cyhal_gpio_init(CYBSP_USER_BTN, CYHAL_GPIO_DIR_INPUT,
                              CYHAL_GPIO_DRIVE_PULLUP, 1);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    Cy_SysTick_Init(CY_SYSTICK_CLOCK_SOURCE_CLK_CPU, SystemCoreClock / 1000);
    Cy_SysTick_SetCallback(0, my_systick_callback);
    Cy_SysTick_Enable();

    cyhal_gpio_init(CYBSP_LED8, CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG, true);

    __enable_irq();

    printf("Systeem gestart. Stuur 's' om te beginnen.\r\n");

    bool     btn_prev  = true;
    uint32_t last_tick = 0;

   for (;;)
{
    // Controleer eerst OF er data is, dan pas lezen
    if (cyhal_uart_readable(&cy_retarget_io_uart_obj) > 0)
    {
        char cmd;
        cyhal_uart_getc(&cy_retarget_io_uart_obj, (uint8_t*)&cmd, 0);
        g_uart_cmd = cmd;
    }

    fsm_event_t evt = poll_event(&btn_prev, &last_tick);
    if (evt != EVT_NONE)
        fsm_handle(evt);

    cyhal_gpio_write(CYBSP_LED8, g_state != ST_LOGGING);
}
}