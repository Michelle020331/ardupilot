/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Code by Andrew Tridgell and Siddharth Bharat Purohit
 */
#pragma once

#include "AP_HAL_ChibiOS.h"
#include "Semaphores.h"

#if HAL_RCINPUT_WITH_AP_RADIO
#include <AP_Radio/AP_Radio.h>
#endif

#include <AP_RCProtocol/AP_RCProtocol.h>

#if HAL_USE_ICU == TRUE
#include "SoftSigReader.h"
#endif

#if HAL_USE_EICU == TRUE
#include "SoftSigReaderInt.h"
#endif

#ifndef RC_INPUT_MAX_CHANNELS
#define RC_INPUT_MAX_CHANNELS 18
#endif

class ChibiOS::RCInput : public AP_HAL::RCInput {
public:
    void init() override;
    bool new_input() override;
    uint8_t num_channels() override;
    uint16_t read(uint8_t ch) override;
    uint8_t read(uint16_t* periods, uint8_t len) override;

    /* enable or disable pulse input for RC input. This is used to
       reduce load when we are decoding R/C via a UART */
    void pulse_input_enable(bool enable) override;

    int16_t get_rssi(void) override {
        return _rssi;
    }
    int16_t get_rx_link_quality(void) override {
        return _rx_link_quality;
    }
    int8_t get_rfmode(void) override {
        return _rfmode;
    }
    int16_t get_tx_power(void) override {
        return _tx_power;
    }
    int8_t get_snr(void) override {
        return _snr;
    }
    int8_t get_active_antenna(void) override {
        return _active_antenna;
    }
    const char *protocol() const override { return last_protocol; }

    void _timer_tick(void);
    bool rc_bind(int dsmMode) override;

private:
    uint16_t _rc_values[RC_INPUT_MAX_CHANNELS] = {0};

    uint64_t _last_read;
    uint8_t _num_channels;
    Semaphore rcin_mutex;
    int16_t _rssi = -1;
    int16_t _rx_link_quality = -1;
    int8_t _rfmode = -1; // RF_MODE_UNKNOWN from AP_RCProtocol_CRSF::RFMode
    int16_t _tx_power = -1;
    int8_t _snr = INT8_MIN;
    int8_t _active_antenna = -1;
    uint32_t _rcin_timestamp_last_signal;
    uint32_t _rcin_last_iomcu_ms;
    bool _init;
    const char *last_protocol;

    enum class RCSource {
        NONE = 0,
        IOMCU = 1,
        RCPROT_PULSES = 2,
        RCPROT_BYTES = 3,
        APRADIO = 4,
    } last_source;

    bool pulse_input_enabled;

#if HAL_RCINPUT_WITH_AP_RADIO
    bool _radio_init;
    AP_Radio *radio;
    uint32_t last_radio_us;
#endif

#if HAL_USE_ICU == TRUE
    ChibiOS::SoftSigReader sig_reader;
#endif

#if HAL_USE_EICU == TRUE
    ChibiOS::SoftSigReaderInt sig_reader;
#endif

#if HAL_WITH_IO_MCU
    uint32_t last_iomcu_us;
#endif
};
