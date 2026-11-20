#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "bytes.h"

void test_put_u32_golden(void)
{
    uint8_t b[4];
    uint8_t expected[4] = {0x01, 0x02, 0x03, 0x04};

    put_u32(b, 0x04030201);

    assert(memcmp(b, expected, sizeof(expected)) == 0);

    printf("test_put_u32_golden: passed\n");
}

void test_put_u64_golden(void)
{
    uint8_t b[8];
    uint8_t expected[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    put_u64(b, 0x0807060504030201ULL);

    assert(memcmp(b, expected, sizeof(expected)) == 0);

    printf("test_put_u64_golden: passed\n");
}

void test_get_u32_golden(void)
{
    uint8_t in[4] = {0x01, 0x02, 0x03, 0x04};

    assert(get_u32(in) == 0x04030201);

    printf("test_get_u32_golden: passed\n");
}

void test_get_u64_golden(void)
{
    uint8_t in[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    assert(get_u64(in) == 0x0807060504030201ULL);

    printf("test_get_u64_golden: passed\n");
}

// a high bit set in each byte position catches sign-extension bugs,
// e.g. a byte promoted through a signed type before shifting
void test_high_bit_golden(void)
{
    uint8_t b32[4];
    uint8_t exp32[4] = {0x00, 0x00, 0x00, 0x80};

    put_u32(b32, 0x80000000u);
    assert(memcmp(b32, exp32, sizeof(exp32)) == 0);
    assert(get_u32(exp32) == 0x80000000u);

    uint8_t in32[4] = {0x80, 0x00, 0x00, 0x00};
    assert(get_u32(in32) == 0x80u);

    uint8_t b64[8];
    uint8_t exp64[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80};

    put_u64(b64, 0x8000000000000000ULL);
    assert(memcmp(b64, exp64, sizeof(exp64)) == 0);
    assert(get_u64(exp64) == 0x8000000000000000ULL);

    uint8_t in64[8] = {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert(get_u64(in64) == 0xFFULL);

    printf("test_high_bit_golden: passed\n");
}

void test_edge_values_round_trip(void)
{
    uint32_t v32[] = {0, 1, 0xFF, 0x100, 0x80000000u, UINT32_MAX};
    uint64_t v64[] = {0, 1, 0xFF, 0x100, 0x80000000u, 0xFFFFFFFFULL,
                      0x100000000ULL, 0x8000000000000000ULL, UINT64_MAX};

    for (size_t i = 0; i < sizeof(v32) / sizeof(v32[0]); i++)
    {
        uint8_t b[4];

        put_u32(b, v32[i]);
        assert(get_u32(b) == v32[i]);
    }

    for (size_t i = 0; i < sizeof(v64) / sizeof(v64[0]); i++)
    {
        uint8_t b[8];

        put_u64(b, v64[i]);
        assert(get_u64(b) == v64[i]);
    }

    printf("test_edge_values_round_trip: passed\n");
}

void test_all_ones_golden(void)
{
    uint8_t b32[4];
    uint8_t b64[8];
    uint8_t ff32[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t ff64[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    put_u32(b32, UINT32_MAX);
    assert(memcmp(b32, ff32, sizeof(ff32)) == 0);

    put_u64(b64, UINT64_MAX);
    assert(memcmp(b64, ff64, sizeof(ff64)) == 0);

    printf("test_all_ones_golden: passed\n");
}

// the helpers must write exactly 4 / 8 bytes and nothing past them
void test_no_overrun(void)
{
    uint8_t b[10];

    memset(b, 0xAA, sizeof(b));
    put_u32(b, 0xFFFFFFFFu);
    assert(b[4] == 0xAA && b[9] == 0xAA);

    memset(b, 0xAA, sizeof(b));
    put_u64(b, UINT64_MAX);
    assert(b[8] == 0xAA && b[9] == 0xAA);

    printf("test_no_overrun: passed\n");
}

int main(void)
{
    test_put_u32_golden();
    test_put_u64_golden();
    test_get_u32_golden();
    test_get_u64_golden();
    test_high_bit_golden();
    test_edge_values_round_trip();
    test_all_ones_golden();
    test_no_overrun();
}
