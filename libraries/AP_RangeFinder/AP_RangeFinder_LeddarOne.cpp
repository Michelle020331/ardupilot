// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <AP_HAL/AP_HAL.h>
#include "AP_RangeFinder_LeddarOne.h"
#include <AP_SerialManager/AP_SerialManager.h>
#include <ctype.h>

extern const AP_HAL::HAL& hal;

// Table for CRC values high byte
static uint8_t CRC_HI[] =
{
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
    0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81,
    0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
    0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
    0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
    0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40
};

// Table for CRC values low byte
static uint8_t CRC_LO[] =
{
    0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7, 0x05, 0xC5, 0xC4,
    0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
    0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD,
    0x1D, 0x1C, 0xDC, 0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
    0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32, 0x36, 0xF6, 0xF7,
    0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
    0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE,
    0x2E, 0x2F, 0xEF, 0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
    0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1, 0x63, 0xA3, 0xA2,
    0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
    0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB,
    0x7B, 0x7A, 0xBA, 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
    0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0, 0x50, 0x90, 0x91,
    0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
    0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88,
    0x48, 0x49, 0x89, 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
    0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83, 0x41, 0x81, 0x80,
    0x40
};


/*
   The constructor also initialises the rangefinder. Note that this
   constructor is not called until detect() returns true, so we
   already know that we should setup the rangefinder
*/
AP_RangeFinder_LeddarOne::AP_RangeFinder_LeddarOne(RangeFinder &_ranger, uint8_t instance,
                                                               RangeFinder::RangeFinder_State &_state,
                                                               AP_SerialManager &serial_manager) :
    AP_RangeFinder_Backend(_ranger, instance, _state)
{
    uart = serial_manager.find_serial(AP_SerialManager::SerialProtocol_Lidar, 0);
    if (uart != nullptr) {
GCS_MAVLINK::send_statustext(MAV_SEVERITY_DEBUG, 0xFF, "LeddarOne: found serial");
        uart->begin(serial_manager.find_baudrate(AP_SerialManager::SerialProtocol_Lidar, 0));
    }
}

/*
   detect if a LeddarOne rangefinder is connected. We'll detect by
   trying to take a reading on Serial. If we get a result the sensor is
   there.
*/
bool AP_RangeFinder_LeddarOne::detect(RangeFinder &_ranger, uint8_t instance, AP_SerialManager &serial_manager)
{
    return serial_manager.find_serial(AP_SerialManager::SerialProtocol_Lidar, 0) != nullptr;
}

// read - return last value measured by sensor
bool AP_RangeFinder_LeddarOne::get_reading(uint16_t &reading_cm)
{
    if (uart == nullptr) {
        return false;
    }

    // send a request message to update register
    if (send_request() < 0) {
        return false;
    }

    uint32_t start_ms = AP_HAL::millis();
	while (!uart->available())
	{
		// wait up to 200ms
		if (AP_HAL::millis() - start_ms > 200) {
			return false;
		}
	}

    // parse a response message, set distance and amplitude to detections
    if (parse_response() < 0) {
        return false;
    }

    reading_cm = sum_distance / number_detections;
gcs_send_text_fmt(MAV_SEVERITY_DEBUG, "LeddarOne: %d cm\n", reading_cm);
    return true;
}

/*
   update the state of the sensor
*/
void AP_RangeFinder_LeddarOne::update(void)
{
    if (get_reading(state.distance_cm)) {
        // update range_valid state based on distance measured
        last_reading_ms = AP_HAL::millis();
        update_status();
    } else if (AP_HAL::millis() - last_reading_ms > 200) {
        set_status(RangeFinder::RangeFinder_NoData);
    }
}

/*
   CRC16 calculate
*/
bool AP_RangeFinder_LeddarOne::CRC16(uint8_t *aBuffer, uint8_t aLength, bool aCheck)
{
    uint8_t lCRCHi = 0xFF;
    uint8_t lCRCLo = 0xFF;
    int i;

    for (i = 0; i<aLength; i++) {
        int lIndex = lCRCLo ^ aBuffer[i];
        lCRCLo = lCRCHi ^ CRC_HI[lIndex];
        lCRCHi = CRC_LO[lIndex];
    }

    if (aCheck) {
        return (aBuffer[aLength] == lCRCLo) && (aBuffer[aLength+1] == lCRCHi);
    } else {
        aBuffer[aLength] = lCRCLo;
        aBuffer[aLength+1] = lCRCHi;
        return true;
    }
}

/*
   send a request message to execute ModBus function
 */
int8_t AP_RangeFinder_LeddarOne::send_request(void)
{
    uint8_t data_buffer[19] = {0};
    uint8_t i = 0;

    int32_t nbytes = uart->available();

    // clear buffer
    while (nbytes-- > 0) {
        uart->read();
        if (++i > 250) {
        	return LEDDARONE_ERR_SERIAL_PORT;
        }
    }

    // Modbus read input register (function code 0x04)
    data_buffer[0] = LEDDARONE_DEFAULT_ADDRESS;
    data_buffer[1] = 0x04;
    data_buffer[2] = 0;
    data_buffer[3] = 20;
    data_buffer[4] = 0;
    data_buffer[5] = 10;

    // CRC16
    CRC16(data_buffer, 6, false);

    // write buffer data with CRC16 bit
    for (i=0; i<8; i++) {
        uart->write(data_buffer[i]);
    }
    uart->flush();

    return 0;
}

 /*
    parse a response message from ModBus
  */
int8_t AP_RangeFinder_LeddarOne::parse_response(void)
{
    uint8_t data_buffer[25] = {0};
    uint32_t start_ms = AP_HAL::millis();
    uint32_t nbytes = 0;
    uint8_t i = 0;
    uint32_t len =0;
    uint8_t index_offset = 11;

    // read serial
    while (AP_HAL::millis() - start_ms < 10) {
        nbytes = uart->available();

        if (len == 25 && nbytes == 0) {
        	break;
        } else {
            for (i=len; i<nbytes+len; i++) {
            	if (i >= 25) {
    GCS_MAVLINK::send_statustext(MAV_SEVERITY_DEBUG, 0xFF, "LeddarOne: BAD_RESPONSE");
            	    	return LEDDARONE_ERR_BAD_RESPONSE;
            	}
                data_buffer[i] = uart->read();
            }

            start_ms = AP_HAL::millis();
            len += nbytes;
        }
    }

gcs_send_text_fmt(MAV_SEVERITY_DEBUG, "LeddarOne: data length %d", len);


    // CRC16
    if (!CRC16(data_buffer, len-2, true)) {
GCS_MAVLINK::send_statustext(MAV_SEVERITY_DEBUG, 0xFF, "LeddarOne: BAD_CRC");
    	return LEDDARONE_ERR_BAD_CRC;
    }

    // number of detections
    number_detections = data_buffer[10];

    // if the number of detection is over , it is false
    if (number_detections > (sizeof(detections) / sizeof(detections[0]))) {
GCS_MAVLINK::send_statustext(MAV_SEVERITY_DEBUG, 0xFF, "LeddarOne: NUMBER_DETECTIONS");
    	return LEDDARONE_ERR_NUMBER_DETECTIONS;
    }

    memset(detections, 0, sizeof(detections));
    sum_distance = 0;
    for (i=0; i<number_detections; i++) {
        detections[i] =  ((uint32_t)data_buffer[index_offset])*256 + data_buffer[index_offset+1];
        sum_distance += detections[i];
        index_offset += 4;
    }

    return number_detections;
}

void AP_RangeFinder_LeddarOne::gcs_send_text_fmt(MAV_SEVERITY severity, const char *fmt, ...)
{
    char str[MAVLINK_MSG_STATUSTEXT_FIELD_TEXT_LEN] {};
    va_list arg_list;
    va_start(arg_list, fmt);
    va_end(arg_list);
    hal.util->vsnprintf((char *)str, sizeof(str), fmt, arg_list);
    GCS_MAVLINK::send_statustext(severity, 0xFF, str);
}

