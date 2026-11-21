#ifndef BYTES_H
#define BYTES_H

#include <stdint.h>

void put_u32(uint8_t *p, uint32_t v);
uint32_t get_u32(const uint8_t *p);
void put_u64(uint8_t *p, uint64_t v);
uint64_t get_u64(const uint8_t *p);

#endif