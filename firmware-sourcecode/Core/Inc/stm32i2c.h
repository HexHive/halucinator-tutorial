#pragma once

uint8_t i2c_write(uint8_t dev_addr,
                 uint8_t *reg_data, 
                 uint32_t len,
                 void* inf_ptr);

uint8_t i2c_read(uint8_t dev_addr, 
                uint8_t *reg_data, 
                uint32_t len,
                void* inf_ptr);

void delay_ms (uint32_t ms);
