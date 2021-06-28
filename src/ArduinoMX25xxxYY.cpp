/*
 * Arduino-MX25xxxYY is an Arduino Library for the Macronix MX25R6435F flash chip.
 * It is a wrapper for the c-MX25xxxYY library.
 * Copyright (C) 2021  eResearch, James Cook University
 * Author: NigelB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Repository: https://github.com/jcu-eresearch/Arduino-MX25xxxYY
 *
 */

#include "ArduinoMX25xxxYY.h"

bool ArduinoMX25xxxYY::begin(MX25xxxYY_Chip_Info_t *chip_def, uint8_t cs_pin, uint8_t reset_pin, uint8_t wp_pin, SPIClass *spi = &SPI)
{
    if(!MX25xxxYY___test_linker(&dev))
    {
        this->println("MX25xxxYY___test_linker returned FALSE. Linking the library has not been done correctly.");
        return false;
    }
    //Configure The Pins
    this->printf("CS Pin: %i\r\n", cs_pin);
    this->printf("RESET Pin: %i\r\n", reset_pin);
    this->printf("WP Pin: %i\r\n", wp_pin);
    pinMode(cs_pin, OUTPUT);
    digitalWrite(cs_pin, HIGH);
    pinMode(reset_pin, OUTPUT);
    digitalWrite(reset_pin, HIGH);
    pinMode(wp_pin, OUTPUT);
    digitalWrite(wp_pin, HIGH);

    spi->begin(); // Call in case SPI.begin hasn't been called
    MX25xxxYY_init(&this->dev, chip_def, cs_pin, reset_pin, wp_pin, 0x00, this);
    readIdentities();
    this->printf("Device State: %i\r\n", dev.state);
    return
        manufacturer_id == chip_def->manufacturer_id &&
        memory_type == chip_def->memory_type &&
        memory_density == chip_def->memory_density;
}

void ArduinoMX25xxxYY::setDebugStream(ArduinoMX25xxxYY_DEBUG_UART_TYPE *stream)
{
    this->debug_stream = stream;
    this->println("Debug Stream enabled on ArduinoMX25xxxYY");
}

ArduinoMX25xxxYY_status ArduinoMX25xxxYY::readIdentities()
{
    manufacturer_id = 0;
    memory_type = 0;
    memory_density = 0;
    return MX25xxxYY_read_identification(&this->dev, &manufacturer_id, &memory_type, &memory_density);
}

int ArduinoMX25xxxYY::getManufacturerId() const {
    return manufacturer_id;
}

int ArduinoMX25xxxYY::getMemoryType() const {
    return memory_type;
}

int ArduinoMX25xxxYY::getMemoryDensity() const {
    return memory_density;
}

ArduinoMX25xxxYY_status ArduinoMX25xxxYY::readFlashMemory(uint32_t memory_address, size_t length, uint8_t *buffer, bool fast_mode) {
    MX25xxxYY_status_enum_t status = MX25xxxYY_status_init;
    memset(buffer, 0, length);
    status = MX25xxxYY_read_stored_data(&this->dev, fast_mode, memory_address, length, buffer);

    return status;
}

ArduinoMX25xxxYY_status ArduinoMX25xxxYY::writeFlashMemory(uint32_t memory_address, size_t length, uint8_t *buffer, bool block)
{
    ArduinoMX25xxxYY_status status = MX25xxxYY_status_init;

    status = MX25xxxYY_set_write_enable(&this->dev, true);
    ArduinoMX25xxxYY_status wait_status = waitForWriteEnableBit(true, dev.chip_def->timing.tWSR);

    if(!MX25xxxYY_HAS_ERROR(wait_status))
    {
        status |= MX25xxxYY_write_stored_data(&this->dev, memory_address, length, buffer);
    }else
    {
        status |= wait_status;
        return status;
    }
    if(block)
    {
        this->println("Waiting for WIP to be false");
        ArduinoMX25xxxYY_status wip_wait_status = waitForWriteInProgressBit(false, dev.chip_def->timing.tBP);
        status |= wip_wait_status;
        this->println(wait_status);
    }

    return status;
}

ArduinoMX25xxxYY_status ArduinoMX25xxxYY::eraseFlashMemory(
        uint32_t block_memory_address,
        MX25xxxYY_Erase_enum_t block_size,
        bool block)
{
    ArduinoMX25xxxYY_status status = MX25xxxYY_status_init;

    status = MX25xxxYY_set_write_enable(&this->dev, true);
    ArduinoMX25xxxYY_status wait_status = waitForWriteEnableBit(true, dev.chip_def->timing.tWSR);
    if(!MX25xxxYY_HAS_ERROR(wait_status))
    {
        status |= MX25xxxYY_erase(&this->dev, block_size, block_memory_address);
    }else
    {
        status = wait_status;
    }
    if(block)
    {
        this->println("Waiting for WIP to be false");
        uint32_t max_block_erasure_time = MX25xxxYY_get_erasure_max_time(&this->dev, block_size);
        status |= waitForWriteInProgressBit(false, max_block_erasure_time);
    }
    status |= MX25xxxYY_set_write_enable(&this->dev, false);
    return status;
}

ArduinoMX25xxxYY_status ArduinoMX25xxxYY::waitForWriteEnableBit(bool to_set, uint32_t max_expected_time_us)
{
    uint8_t status_reg = 0;

    uint32_t time_left = max_expected_time_us;
    // timeout after 1.5 times the max_expected_time_us
    uint32_t adjusted_max_time = (uint32_t)(((double)max_expected_time_us) * 1.5);

    // Wait until the status register WEL bit is set to to_set or the operation times out.
    unsigned long start = micros();
    do
    {
        // timeout after 1.5 times the max_expected_time_us
        if((micros() - start) > (adjusted_max_time))
        {
            MX25xxxYY_read_status_register(&this->dev, &status_reg);
            if(MX25xxxYY_SR_WEL_GET_VALUE(status_reg) != to_set) {
                return MX25xxxYY_status_ok;
            }
            return MX25xxxYY_status_error_timeout;
        }
        MX25xxxYY_read_status_register(&this->dev, &status_reg);
        this->printf("WEL is %s\r\n", MX25xxxYY_SR_WEL_GET_VALUE(status_reg)?"true":"false");
        if(MX25xxxYY_SR_WEL_GET_VALUE(status_reg) != to_set) {
            delayMicroseconds(time_left / 2);

            // Never delayMicroseconds for less the 100 micro-seconds
            if (time_left > 200) {
                time_left -= (time_left / 2);
            } else {
                time_left = 200;
            }
        }
    }while(MX25xxxYY_SR_WEL_GET_VALUE(status_reg) != to_set);
    return MX25xxxYY_status_ok;
}

ArduinoMX25xxxYY_status ArduinoMX25xxxYY::waitForWriteInProgressBit(bool to_set, uint32_t max_expected_time_us)
{
    uint8_t status_reg = 0;
    uint32_t time_left = max_expected_time_us;
    // Wait until the status register WEL bit is set to to_set or the operation times out.
    unsigned long start = micros();
    // timeout after 1.5 times the max_expected_time_us
    uint32_t adjusted_max_time = (uint32_t)(((double)max_expected_time_us) * 1.5);
    do
    {
        if((micros() - start) > (adjusted_max_time))
        {
            MX25xxxYY_read_status_register(&this->dev, &status_reg);
            if(MX25xxxYY_SR_WIP_GET_VALUE(status_reg) != to_set) {
                return MX25xxxYY_status_ok;
            }
            return MX25xxxYY_status_error_timeout;
        }
        MX25xxxYY_read_status_register(&this->dev, &status_reg);
        this->printf("WIP is %s\r\n", MX25xxxYY_SR_WIP_GET_VALUE(status_reg)?"true":"false");
        if(MX25xxxYY_SR_WIP_GET_VALUE(status_reg) != to_set)
        {
            delayMicroseconds(time_left / 2);

            // Never delayMicroseconds for less the 100 micro-seconds
            if(time_left > 200) {
                time_left -= (time_left / 2);
            }else
            {
                time_left = 200;
            }
        }
    }while(MX25xxxYY_SR_WIP_GET_VALUE(status_reg) != to_set);
    return MX25xxxYY_status_ok;
}

void ArduinoMX25xxxYY::resetChip()
{
    MX25xxxYY___enable_reset_pin(&this->dev, true);
    delay(100);
    MX25xxxYY___enable_reset_pin(&this->dev, false);
}

const char* ArduinoMX25xxxYY::getChipName()
{
    return dev.chip_def->name;
}

const MX25xxxYY_Chip_Info_t* ArduinoMX25xxxYY::getChipDefinition()
{
    return this->dev.chip_def;
}

int ArduinoMX25xxxYY::println()
{
    if(this->debug_stream != nullptr)
    {
        this->debug_stream->println();
    }
    return 0;
}
