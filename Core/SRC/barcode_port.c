#include "barcode_port.h"
#include "barcode_app.h"
#include "barcode_rx.h"
#include "host_protocol.h"
#ifdef BARCODE_ECHO_MODE
#include "barcode_view.h"
#endif
#ifdef BARCODE_HOST_TEST
#include "hal_stub.h"
#else
#include "usart.h"
#include "gpio.h"
#endif
#include <string.h>
#define RX_CAPACITY 128u
#define HOST_TX_CAPACITY 16u
typedef struct { uint32_t received_at; uint8_t channel, byte; } RxEvent;
typedef struct { uint8_t data[HOST_TX_MAX]; uint8_t len; uint8_t port; } TxFrame;
typedef struct {
    TxFrame *frames;
    uint8_t capacity, head, count;
    bool active, failed;
    volatile bool completed;
} TxQueue;
static RxEvent rx_events[RX_CAPACITY];
static volatile uint16_t rx_head, rx_tail;
static uint8_t rx_byte[2];
static TxFrame host_frames[HOST_TX_CAPACITY];
static TxQueue host_tx;
static BarcodeRx scanner;
static HostParser host;
static volatile bool fault, reset_parsers;
static volatile uint8_t recover_rx;
static volatile BarcodePortStats stats;
static uint32_t lock_irq(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return mask;
}
static void unlock_irq(uint32_t mask) { __DMB(); __set_PRIMASK(mask); }
static UART_HandleTypeDef *uart(uint8_t ch) { return ch == 0u ? &husart1 : &husart2; }
static int channel(UART_HandleTypeDef *u)
{
    if (u == &husart1) return 0;
    if (u == &husart2) return 1;
    return -1;
}
static void mark_rx_fault(uint8_t ch)
{
    fault = true;
    reset_parsers = true;
    recover_rx |= (uint8_t)(1u << ch);
}
static void arm_rx(uint8_t ch)
{
    if (HAL_UART_Receive_IT(uart(ch), &rx_byte[ch], 1) != HAL_OK) {
        ++stats.rearm_errors;
        mark_rx_fault(ch);
    }
}
void BarcodePort_Init(void)
{
    uint32_t mask = lock_irq();
    rx_head = rx_tail = 0;
    memset(&host_tx, 0, sizeof(host_tx));
    host_tx.frames = host_frames; host_tx.capacity = HOST_TX_CAPACITY;
    fault = reset_parsers = false;
    recover_rx = 0;
    stats.rx_overflow = stats.uart_errors = stats.rearm_errors = stats.tx_errors = 0;
    BarcodeRx_Init(&scanner);
    HostParser_Init(&host);
    arm_rx(0); arm_rx(1);
    unlock_irq(mask);
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *u)
{
    int ch = channel(u);
    uint32_t mask;
    if (ch < 0) return;
    mask = lock_irq();
    /* HAL may deliver an errored RX byte before invoking ErrorCallback. */
    if (u->ErrorCode != HAL_UART_ERROR_NONE) {
        mark_rx_fault((uint8_t)ch);
        unlock_irq(mask);
        return;
    }
    if ((uint16_t)(rx_head - rx_tail) == RX_CAPACITY) {
        ++stats.rx_overflow;
        fault = reset_parsers = true;
    } else {
        RxEvent *e = &rx_events[rx_head % RX_CAPACITY];
        e->received_at = HAL_GetTick();
        e->channel = (uint8_t)ch;
        e->byte = rx_byte[ch];
        __DMB();
        ++rx_head;
    }
    arm_rx((uint8_t)ch);
    unlock_irq(mask);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *u)
{
    int ch = channel(u);
    uint32_t mask;
    if (ch < 0) return;
    mask = lock_irq();
    ++stats.uart_errors;
    mark_rx_fault((uint8_t)ch);
    unlock_irq(mask);
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *u)
{
    int ch = channel(u);
    if (ch >= 0) host_tx.completed = true;
}
bool BarcodePort_HasFault(void) { return fault; }
void BarcodePort_GetStats(BarcodePortStats *out)
{
    uint32_t mask = lock_irq();
    *out = stats;
    unlock_irq(mask);
}
static bool enqueue(const uint8_t *data, uint8_t len, uint8_t port)
{
    TxQueue *q = &host_tx;
    TxFrame *f;
    uint32_t mask = lock_irq();
    if (len == 0u || len > HOST_TX_MAX || q->failed) {
        fault = true;
        ++stats.tx_errors;
        unlock_irq(mask);
        return false;
    }
    if (q->count == q->capacity && q->completed && q->active) {
        /* The head frame finished transmitting, so its buffer is free before
           the next Poll would otherwise release it. */
        q->completed = false;
        q->active = false;
        q->head = (uint8_t)((q->head + 1u) % q->capacity);
        --q->count;
    }
    if (q->count == q->capacity) {
        fault = true;
        ++stats.tx_errors;
        unlock_irq(mask);
        return false;
    }
    f = &q->frames[(q->head + q->count) % q->capacity];
    memcpy(f->data, data, len);
    f->len = len;
    f->port = port;
    ++q->count;
    unlock_irq(mask);
    return true;
}
bool BarcodePort_SendHost(const uint8_t *data, uint8_t len)
{ return enqueue(data, len, BARCODE_PORT_HOST); }
bool BarcodePort_SendRescan(const uint8_t *data, uint8_t len)
{ return enqueue(data, len, BARCODE_PORT_SCANNER); }
static void service_tx(void)
{
    TxQueue *q = &host_tx;
    uint32_t mask = lock_irq();
    if (q->completed) {
        q->completed = false;
        if (q->active && q->count != 0u) {
            q->active = false;
            q->head = (uint8_t)((q->head + 1u) % q->capacity);
            --q->count;
        }
    }
    if (!q->active && !q->failed && q->count != 0u) {
        TxFrame *f = &q->frames[q->head];
        HAL_StatusTypeDef result = HAL_UART_Transmit_IT(uart(f->port), f->data, f->len);
        if (result == HAL_OK) q->active = true;
        else if (result != HAL_BUSY) { q->failed = true; fault = true; ++stats.tx_errors; }
    }
    unlock_irq(mask);
}
static void recover(void)
{
    uint8_t ch;
    uint32_t mask = lock_irq();
    if (reset_parsers) {
        /* A missing byte destroys ordering: never join data across that gap. */
        rx_tail = rx_head;
        BarcodeRx_Invalidate(&scanner);
        HostParser_Init(&host);
        reset_parsers = false;
    }
    for (ch = 0; ch < 2u; ++ch) {
        uint8_t bit = (uint8_t)(1u << ch);
        if ((recover_rx & bit) != 0u) {
            recover_rx &= (uint8_t)~bit;
            if (HAL_UART_AbortReceive(uart(ch)) == HAL_OK) arm_rx(ch);
            else { recover_rx |= bit; ++stats.rearm_errors; }
        }
    }
    unlock_irq(mask);
    if (fault) BarcodeApp_CommunicationFault();
}
void BarcodePort_Poll(void)
{
    uint16_t serviced;
    uint32_t mask;
    recover();
    /* Process incoming barcodes before transmitting queued frames. */
    for (serviced = 0; serviced < RX_CAPACITY; ++serviced) {
        RxEvent e;
        mask = lock_irq();
        if (rx_tail == rx_head || reset_parsers) { unlock_irq(mask); break; }
        e = rx_events[rx_tail % RX_CAPACITY];
        ++rx_tail;
        unlock_irq(mask);
        if (fault) BarcodeApp_CommunicationFault();
        BarcodeApp_AdvanceTime(e.received_at);
#ifdef BARCODE_ECHO_MODE
        if (e.channel == BARCODE_ECHO_PORT) BarcodeEcho_OnByte(e.byte);
#endif
        if (e.channel == 1u) {
            Barcode b;
            if (BarcodeRx_Feed(&scanner, e.byte, BarcodeApp_IsBusy(), &b)) BarcodeApp_OnBarcode(&b, HAL_GetTick());
        } else {
            HostFrame fr = HostParser_Feed(&host, e.byte);
            if (fr.ok) BarcodeApp_OnFrame(&fr, HAL_GetTick());
        }
    }
    recover();
    /* Atomically establish an empty queue before advancing to real time.
       Otherwise an IRQ could enqueue a pre-unlock scan between these steps. */
    mask = lock_irq();
    if (rx_tail == rx_head && !reset_parsers) BarcodeApp_Tick(HAL_GetTick());
    unlock_irq(mask);
    service_tx();
    if (fault) BarcodeApp_CommunicationFault();
}
void BarcodePort_SetRelay(bool active)
{
    HAL_GPIO_WritePin(KEY_Con_Port, KEY_Con_Pin, active ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
void BarcodePort_SetLights(bool red, bool green, bool yellow)
{
    if (!red) HAL_GPIO_WritePin(LED_R_Port, LED_R_Pin, GPIO_PIN_RESET);
    if (!green) HAL_GPIO_WritePin(LED_G_Port, LED_G_Pin, GPIO_PIN_RESET);
    if (!yellow) HAL_GPIO_WritePin(LED_B_Port, LED_B_Pin, GPIO_PIN_RESET);
    if (red) HAL_GPIO_WritePin(LED_R_Port, LED_R_Pin, GPIO_PIN_SET);
    if (green) HAL_GPIO_WritePin(LED_G_Port, LED_G_Pin, GPIO_PIN_SET);
    if (yellow) HAL_GPIO_WritePin(LED_B_Port, LED_B_Pin, GPIO_PIN_SET);
}
