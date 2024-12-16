// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include "dataflow_api.h"
#include "ethernet/dataflow_api.h"
#include "noc_parameters.h"
#include "tt_metal/impl/dispatch/kernels/packet_queue_ctrl.hpp"

namespace packet_queue {

constexpr uint32_t NUM_WR_CMD_BUFS = 4;

constexpr uint32_t DEFAULT_MAX_NOC_SEND_WORDS =
    (NUM_WR_CMD_BUFS - 1) * (NOC_MAX_BURST_WORDS * NOC_WORD_BYTES) / PACKET_WORD_SIZE_BYTES;
constexpr uint32_t DEFAULT_MAX_ETH_SEND_WORDS = 2 * 1024;

// Max 2 bits
enum class PtrUpdateType : uint8_t {
    NONE = 0,
    WPTR = 1,
    RPTR_SENT = 2,
    RPTR_CLEARED = 3,
};

// Base for remote network controller.
// T is the implementation.
template <typename T>
class packet_queue_remote_control_t {
private:
    inline T& impl() noexcept { return static_cast<T&>(*this); }

    inline const T& impl() const noexcept { return static_cast<const T&>(*this); }

protected:
    packet_queue_remote_control_t() = default;
    ~packet_queue_remote_control_t() = default;

public:
    // Kernel init
    void init(uint32_t local_ptrs_addr, uint32_t remote_ptrs_addr) {
        this->impl()._init(local_ptrs_addr, remote_ptrs_addr);
    }

    // Set stream register value
    inline void reg_update(uint32_t reg_addr, uint32_t val, uint32_t remote_x, uint32_t remote_y) {
        this->impl()._reg_update(reg_addr, val, remote_x, remote_y);
    }

    // Update a pointer on the remote
    inline void ptr_update(
        uint32_t src_addr, uint32_t dest_addr, PtrUpdateType update_type, uint32_t remote_x, uint32_t remote_y) {
        this->impl()._ptr_update(src_addr, dest_addr, update_type, remote_x, remote_y);
    }

    // Send data to the remote
    inline void send_data(
        uint32_t src_addr, uint32_t dest_addr, uint32_t num_words, uint32_t remote_x, uint32_t remote_y) {
        this->impl()._send_data(src_addr, dest_addr, num_words, remote_x, remote_y);
    }

    // Returns true if the controller is busy and cannot be used yet
    inline bool busy() const { return this->impl()._busy(); }

    // Handle any pending acks from the remote sender
    inline void handle_recv() { this->impl()._handle_recv(); }
};  // packet_queue_remote_control_t

// Remote updates over NOC0.
class packet_queue_remote_noc0_impl final : public packet_queue_remote_control_t<packet_queue_remote_noc0_impl> {
public:
    void _init(uint32_t local_ptrs_addr, uint32_t remote_ptrs_addr) {}

    inline void _reg_update(uint32_t reg_addr, uint32_t val, uint32_t remote_x, uint32_t remote_y) {
        noc_inline_dw_write(get_noc_addr(remote_x, remote_y, reg_addr), val);
    }

    inline void _ptr_update(
        uint32_t src_addr, uint32_t dest_addr, PtrUpdateType update_type, uint32_t remote_x, uint32_t remote_y) {
        noc_inline_dw_write(
            get_noc_addr(remote_x, remote_y, dest_addr), *reinterpret_cast<volatile uint32_t*>(src_addr));
    }

    inline void _send_data(
        uint32_t src_addr, uint32_t dest_addr, uint32_t num_words, uint32_t remote_x, uint32_t remote_y) {
        noc_async_write(
            src_addr,
            get_noc_addr(remote_x, remote_y, dest_addr),
            num_words * 16  // bytes
        );
    }

    inline bool _busy() const { return false; }

    inline void _handle_recv() {}
};  // packet_queue_remote_noc0_impl

// Remote updates over Ethernet.
class packet_queue_remote_eth_impl final : public packet_queue_remote_control_t<packet_queue_remote_eth_impl> {
private:
    volatile uint32_t* sent;
    volatile uint32_t* recv;
    uint32_t remote_sent_addr;
    uint32_t remote_recv_addr;

public:
    void _init(uint32_t ptrs_addr, uint32_t remote_ptrs_addr) {
        // this->sent = reinterpret_cast<volatile uint32_t*>(packet_queue_ptr_buffer_layout_t::get_eth_sent(ptrs_addr));
        // this->recv = reinterpret_cast<volatile uint32_t*>(packet_queue_ptr_buffer_layout_t::get_eth_recv(ptrs_addr));
        // this->remote_sent_addr =
        //     reinterpret_cast<uint32_t>(packet_queue_ptr_buffer_layout_t::get_eth_sent(remote_ptrs_addr));
        // this->remote_recv_addr =
        //     reinterpret_cast<uint32_t>(packet_queue_ptr_buffer_layout_t::get_eth_recv(remote_ptrs_addr));
        // *this->sent = 0;
        // *this->recv = 0;
    }

    inline void _reg_update(uint32_t reg_addr, uint32_t val, uint32_t remote_x, uint32_t remote_y) {
        internal_::eth_write_remote_reg(0, reg_addr, val);
    }

    inline void _ptr_update(
        uint32_t src_addr, uint32_t dest_addr, PtrUpdateType update_type, uint32_t remote_x, uint32_t remote_y) {
        // Need to replace this with sending only 1 packet
        internal_::eth_send_packet(
            0,               // txq
            src_addr >> 4,   // source in words
            dest_addr >> 4,  // dest in words
            1                // words
        );
        *this->sent = 1;
        internal_::eth_send_packet(
            0,                            // txq
            (uint32_t)this->sent >> 4,    // source in words
            this->remote_recv_addr >> 4,  // dest in words
            1                             // words
        );
    }

    inline void _send_data(
        uint32_t src_addr, uint32_t dest_addr, uint32_t num_words, uint32_t remote_x, uint32_t remote_y) {
        internal_::eth_send_packet(0, src_addr >> 4, dest_addr >> 4, num_words);
    }

    inline bool _busy() const { return (bool)*this->sent; /* there is pending data in the sent buffer */ }

    inline void _handle_recv() {
        if (!*this->recv) {
            return;
        }

        // uint32_t value = this->recv->fields.ptr_value;
        // uint32_t dest_addr = this->recv->fields.dest_addr;
        // *reinterpret_cast<volatile uint32_t*>(dest_addr) = value;
        *this->recv = 0;
        internal_::eth_send_packet(
            0,                            // txq
            (uint32_t)this->recv >> 4,    // source in words
            this->remote_sent_addr >> 4,  // dest in words
            1                             // words
        );
    }
};  // packet_queue_remote_eth_impl

// Dummy remote update class for testing.
class packet_queue_remote_nop_impl final : public packet_queue_remote_control_t<packet_queue_remote_nop_impl> {
public:
    void _init(uint32_t local_ptrs_addr, uint32_t remote_ptrs_addr) {}

    inline void _reg_update(uint32_t reg_addr, uint32_t val, uint32_t remote_x, uint32_t remote_y) {}

    inline void _ptr_update(
        uint32_t src_addr, uint32_t dest_addr, PtrUpdateType update_type, uint32_t remote_x, uint32_t remote_y) {}

    inline void _send_data(
        uint32_t src_addr, uint32_t dest_addr, uint32_t num_word, uint32_t remote_x, uint32_t remote_ys) {}

    inline bool _busy() const { return false; }

    inline void _handle_recv() {}
};  // packet_queue_remote_nop_impl

// Query the maximum words that can be sent through each remote type
template <typename T>
struct remote_max_send_words {
    static constexpr uint32_t value = 0;
};

template <>
struct remote_max_send_words<packet_queue_remote_noc0_impl> {
    static constexpr uint32_t value = DEFAULT_MAX_NOC_SEND_WORDS;
};

template <>
struct remote_max_send_words<packet_queue_remote_eth_impl> {
    static constexpr uint32_t value = DEFAULT_MAX_ETH_SEND_WORDS;
};

template <>
struct remote_max_send_words<packet_queue_remote_nop_impl> {
    static constexpr uint32_t value = DEFAULT_MAX_NOC_SEND_WORDS;
};

template <typename T>
constexpr uint32_t remote_max_send_words_v = remote_max_send_words<T>::value;

}  // namespace packet_queue
