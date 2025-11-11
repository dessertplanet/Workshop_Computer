#include "usb_lockfree.h"
#include "pico/stdlib.h"
#include <string.h>

// ============================================================================
// RX Queue - USB → Application
// ============================================================================

static volatile usb_rx_message_t g_usb_rx_queue[USB_RX_QUEUE_SIZE];
static volatile uint32_t g_rx_write_idx = 0;
static volatile uint32_t g_rx_read_idx = 0;
static volatile uint32_t g_rx_drop_count = 0;

void usb_rx_lockfree_init(void) {
    g_rx_write_idx = 0;
    g_rx_read_idx = 0;
    g_rx_drop_count = 0;
    memset((void*)g_usb_rx_queue, 0, sizeof(g_usb_rx_queue));
}

bool usb_rx_lockfree_post(const char* data, uint16_t length) {
    if (length > USB_RX_MSG_MAX_LENGTH) {
        length = USB_RX_MSG_MAX_LENGTH;
    }
    
    uint32_t next_write = (g_rx_write_idx + 1) % USB_RX_QUEUE_SIZE;
    
    // Queue full check
    if (next_write == g_rx_read_idx) {
        g_rx_drop_count++;
        return false;
    }
    
    // Copy message
    memcpy((void*)g_usb_rx_queue[g_rx_write_idx].data, data, length);
    g_usb_rx_queue[g_rx_write_idx].length = length;
    g_usb_rx_queue[g_rx_write_idx].timestamp_us = time_us_32();
    
    // Commit write (atomic on RP2040 single-core access)
    g_rx_write_idx = next_write;
    
    return true;
}

bool usb_rx_lockfree_get(usb_rx_message_t* msg) {
    if (g_rx_read_idx == g_rx_write_idx) {
        return false;  // Queue empty
    }
    
    // Copy message out
    memcpy(msg, (void*)&g_usb_rx_queue[g_rx_read_idx], sizeof(usb_rx_message_t));
    
    // Advance read pointer
    g_rx_read_idx = (g_rx_read_idx + 1) % USB_RX_QUEUE_SIZE;
    
    return true;
}

uint32_t usb_rx_lockfree_pending_count(void) {
    uint32_t w = g_rx_write_idx;
    uint32_t r = g_rx_read_idx;
    if (w >= r) {
        return w - r;
    }
    return USB_RX_QUEUE_SIZE - r + w;
}

uint32_t usb_rx_lockfree_drop_count(void) {
    return g_rx_drop_count;
}

// ============================================================================
// TX Queue - Application → USB
// ============================================================================

static volatile usb_tx_message_t g_usb_tx_queue[USB_TX_QUEUE_SIZE];
static volatile uint32_t g_tx_write_idx = 0;
static volatile uint32_t g_tx_read_idx = 0;
static volatile uint32_t g_tx_drop_count = 0;

void usb_tx_lockfree_init(void) {
    g_tx_write_idx = 0;
    g_tx_read_idx = 0;
    g_tx_drop_count = 0;
    memset((void*)g_usb_tx_queue, 0, sizeof(g_usb_tx_queue));
}

bool usb_tx_lockfree_post(const char* data, uint16_t length, bool needs_flush) {
    if (length > USB_TX_MSG_MAX_LENGTH) {
        length = USB_TX_MSG_MAX_LENGTH;
    }
    
    uint32_t next_write = (g_tx_write_idx + 1) % USB_TX_QUEUE_SIZE;
    
    // Queue full check
    if (next_write == g_tx_read_idx) {
        g_tx_drop_count++;
        return false;
    }
    
    // Copy message
    memcpy((void*)g_usb_tx_queue[g_tx_write_idx].data, data, length);
    g_usb_tx_queue[g_tx_write_idx].length = length;
    g_usb_tx_queue[g_tx_write_idx].needs_flush = needs_flush;
    
    // Commit write (atomic on RP2040 single-core access)
    g_tx_write_idx = next_write;
    
    return true;
}

bool usb_tx_lockfree_get(usb_tx_message_t* msg) {
    if (g_tx_read_idx == g_tx_write_idx) {
        return false;  // Queue empty
    }
    
    // Copy message out
    memcpy(msg, (void*)&g_usb_tx_queue[g_tx_read_idx], sizeof(usb_tx_message_t));
    
    // Advance read pointer
    g_tx_read_idx = (g_tx_read_idx + 1) % USB_TX_QUEUE_SIZE;
    
    return true;
}

uint32_t usb_tx_lockfree_pending_count(void) {
    uint32_t w = g_tx_write_idx;
    uint32_t r = g_tx_read_idx;
    if (w >= r) {
        return w - r;
    }
    return USB_TX_QUEUE_SIZE - r + w;
}

uint32_t usb_tx_lockfree_drop_count(void) {
    return g_tx_drop_count;
}

// ============================================================================
// Combined Initialization
// ============================================================================

void usb_lockfree_init(void) {
    usb_rx_lockfree_init();
    usb_tx_lockfree_init();
}
