#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

#include "kv.h"

static char *data[][2] = {
    {"fruit", "mango"},
    {"vegetable", "potato"},
    {"tree", "banyan"}};

#define N ((int)(sizeof(data) / sizeof(data[0])))

// bytes of key + value in the last record of data
static size_t last_payload_len(void)
{
    return strlen(data[N - 1][0]) + strlen(data[N - 1][1]);
}

static off_t log_size(void)
{
    struct stat st;
    assert(stat("data.log", &st) != -1);
    return st.st_size;
}

// asserts the key is present with exactly the expected value; frees it
static void assert_get(char *key, char *expected)
{
    char *value = NULL;

    assert(get(key, &value) == 1);
    assert(strcmp(expected, value) == 0);

    free(value);
}

static void assert_absent(char *key)
{
    char *value = NULL;

    assert(get(key, &value) == 0);
}

// puts every record of data; returns the log size just before the last one
static off_t put_all(void)
{
    off_t before_last = 0;

    for (int i = 0; i < N; i++)
    {
        if (i == N - 1)
            before_last = log_size();

        assert(put(data[i][0], data[i][1]) == 1);
    }

    return before_last;
}

// all records except the last are readable, the last is gone
static void assert_good_records(void)
{
    for (int i = 0; i < N - 1; i++)
        assert_get(data[i][0], data[i][1]);

    assert_absent(data[N - 1][0]);
}

// the torn record was cut off, and the store accepts writes afterwards
static void assert_recovered_and_writable(off_t trunc_size)
{
    assert_good_records();

    assert(log_size() == trunc_size);

    assert(put("river", "ganga") == 1);

    shutdown();

    assert(bootup() == 1);

    assert_good_records();
    assert_get("river", "ganga");
}

static void flip_byte(off_t offset)
{
    int fd = open("data.log", O_RDWR, 0644);
    assert(fd != -1);

    uint8_t byte;

    assert(pread(fd, &byte, 1, offset) == 1);

    byte ^= 1;

    assert(pwrite(fd, &byte, 1, offset) == 1);

    close(fd);
}

static void truncate_log(off_t size)
{
    assert(truncate("data.log", size) != -1);
}

void test_bootup(void)
{
    reset();

    assert(bootup() == 1);

    printf("test_bootup: passed\n");
}

void test_put_and_get(void)
{
    reset();

    assert(bootup() == 1);

    assert(put("hello", "world") == 1);

    assert_get("hello", "world");

    printf("test_put_and_get: passed\n");
}

void test_put_shutdown_and_get(void)
{
    reset();

    assert(bootup() == 1);

    assert(put("hello", "world") == 1);

    shutdown();

    assert(bootup() == 1);

    assert_get("hello", "world");

    printf("test_put_shutdown_and_get: passed\n");
}

void test_put_shutdown_and_get_absent(void)
{
    reset();

    assert(bootup() == 1);

    assert(put("hello", "world") == 1);

    shutdown();

    assert(bootup() == 1);

    assert_absent("hello100");

    printf("test_put_shutdown_and_get_absent: passed\n");
}

void test_key_override(void)
{
    reset();

    assert(bootup() == 1);

    assert(put("hello", "world") == 1);

    shutdown();

    assert(bootup() == 1);

    assert(put("hello", "darkness") == 1);

    shutdown();

    assert(bootup() == 1);

    assert_get("hello", "darkness");

    printf("test_key_override: passed\n");
}

void test_delete(void)
{
    reset();

    assert(bootup() == 1);

    assert(put("hello", "world") == 1);

    assert(delete("hello") == 1);

    assert_absent("hello");

    shutdown();

    assert(bootup() == 1);

    assert_absent("hello");

    printf("test_delete: passed\n");
}

void test_torn_tail_payload(void)
{
    reset();

    assert(bootup() == 1);

    off_t trunc_size = put_all();

    shutdown();

    // cut the last byte of the last record's value
    truncate_log(log_size() - 1);

    assert(bootup() == 1);

    assert_recovered_and_writable(trunc_size);

    printf("test_torn_tail_payload: passed\n");
}

void test_torn_tail_header(void)
{
    reset();

    assert(bootup() == 1);

    off_t trunc_size = put_all();

    shutdown();

    // leave the last record with one byte short of a full header
    truncate_log(log_size() - last_payload_len() - 1);

    assert(bootup() == 1);

    assert_recovered_and_writable(trunc_size);

    printf("test_torn_tail_header: passed\n");
}

void test_tail_corruption(void)
{
    reset();

    assert(bootup() == 1);

    put_all();

    shutdown();

    // flip the last header byte of the last record; its length is unchanged
    flip_byte(log_size() - last_payload_len() - 1);

    assert(bootup() == 1);

    assert_good_records();

    printf("test_tail_corruption: passed\n");
}

void test_mid_file_corruption(void)
{
    reset();

    assert(bootup() == 1);

    put_all();

    shutdown();

    // flip a byte inside the first record
    flip_byte(1);

    assert(bootup() == 0);

    printf("test_mid_file_corruption: passed\n");
}

int main(void)
{
    test_bootup();
    test_put_and_get();
    test_put_shutdown_and_get();
    test_put_shutdown_and_get_absent();
    test_key_override();
    test_delete();
    test_torn_tail_payload();
    test_torn_tail_header();
    test_tail_corruption();
    test_mid_file_corruption();
}
