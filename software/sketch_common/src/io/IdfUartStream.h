// IdfUartStream — Arduino Stream wrapper around the ESP-IDF UART driver.
//
// Created in PR #609 so EfisReadTask can block on the IDF UART event queue
// (wake-on-data, not busy-poll) while EfisSerialPort::Read() still drains
// bytes via the standard Stream::available()/read() API.
//
// Why not just Arduino HardwareSerial: HardwareSerial wraps the same IDF
// driver but does not expose the event queue. Blocking the task on
// xQueueReceive against the IDF queue is the only way to wake the task
// immediately when bytes arrive at the UART; otherwise we busy-poll
// available() and have the same problem we're trying to leave behind.
//
// Why not bypass Stream and have EfisReadTask call uart_read_bytes() +
// parser_.FeedByte() directly: keeps EfisSerialPort::Read() — and the
// SyntheticStream-swap that the perf-synth env relies on — unchanged.
// The wrapper is ~80 LoC and pays for itself.
//
// Coexistence with Arduino HardwareSerial: NONE. uart_driver_install
// takes exclusive ownership of the UART. The caller (setup() in the
// .ino) must NOT call Serial2.begin() if it uses this for UART 2; and
// since Serial2 is the only consumer of UART 2 in OnSpeed, that's
// straightforward. Serial1 is shared between boom (RX) and display
// (TX); we therefore use Arduino HardwareSerial for Serial1, NOT this
// class. EFIS-on-Serial2 is the only place this is wired.

#pragma once

#include <Arduino.h>
#include <Stream.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class IdfUartStream : public Stream {
public:
    IdfUartStream() = default;

    // Install the IDF UART driver, configure pins, and set up an event
    // queue. After Begin() returns, GetEventQueue() yields the queue that
    // the consuming task should xQueueReceive on; bytes are then drained
    // via Stream::read().
    //
    // rxBufferSize is the IDF software RX buffer (NOT the 128 B hardware
    // FIFO). 2 KB is 16× a VN-300 frame; absorbs preemption bursts.
    //
    // Returns true on success. On failure (driver install error) returns
    // false; the queue is null and the Stream produces no bytes — caller
    // must check.
    bool Begin(uart_port_t port, int rxPin, int txPin,
               int baud = 115200,
               size_t rxBufferSize = 2048,
               size_t queueLength = 16) {
        port_ = port;

        uart_config_t cfg = {};
        cfg.baud_rate           = baud;
        cfg.data_bits           = UART_DATA_8_BITS;
        cfg.parity              = UART_PARITY_DISABLE;
        cfg.stop_bits           = UART_STOP_BITS_1;
        cfg.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
        cfg.rx_flow_ctrl_thresh = 0;
        cfg.source_clk          = UART_SCLK_DEFAULT;

        if (uart_param_config(port_, &cfg) != ESP_OK) return false;
        if (uart_set_pin(port_, txPin, rxPin,
                         UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
            return false;
        // queueLength sized to absorb ~queueLength × event-batches of
        // RX bursts before the consumer task wakes. 16 is comfortable.
        if (uart_driver_install(port_, rxBufferSize, /*tx_buf=*/0,
                                queueLength, &eventQueue_, 0) != ESP_OK) {
            eventQueue_ = nullptr;
            return false;
        }
        return true;
    }

    // The FreeRTOS queue that emits uart_event_t when bytes arrive.
    // Consumer tasks block on this with xQueueReceive.
    QueueHandle_t GetEventQueue() const { return eventQueue_; }

    // ===== Arduino Stream interface =====

    int available() override {
        size_t n = 0;
        // Returns 0 on error; we'd rather report 0 than fault.
        if (uart_get_buffered_data_len(port_, &n) != ESP_OK) return 0;
        return static_cast<int>(n);
    }

    int read() override {
        uint8_t b;
        // 0 ticks = non-blocking; we already know data is available via
        // available()/event-queue gate, so this is a fast path.
        const int got = uart_read_bytes(port_, &b, 1, 0);
        return (got == 1) ? static_cast<int>(b) : -1;
    }

    int peek() override {
        // ESP-IDF UART driver doesn't expose a peek primitive; we'd need
        // a one-byte readahead buffer here to support it. Nothing in
        // EfisSerialPort::Read() calls peek(), so leaving this as -1 is
        // safe. If a future caller needs peek, add the readahead.
        return -1;
    }

    void flush() override {
        // Stream::flush is meant to flush WRITES; we don't write. No-op.
    }

    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }

private:
    uart_port_t   port_       = UART_NUM_MAX;
    QueueHandle_t eventQueue_ = nullptr;
};
