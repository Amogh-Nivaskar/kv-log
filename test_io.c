#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "io.h"

#define SCRATCH "test_io_scratch.log"

static int open_scratch(void)
{
    int fd = open(SCRATCH, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert(fd != -1);
    return fd;
}

static void close_scratch(int fd)
{
    close(fd);
    unlink(SCRATCH);
}

// deterministic, non-repeating-looking test pattern
static void fill_pattern(uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)(i * 31 + (i >> 8));
}

void test_write_then_read_back(void)
{
    int fd = open_scratch();

    char out[] = "hello, world";
    char in[sizeof(out)] = {0};

    assert(write_full(fd, out, sizeof(out), 0) == (int)sizeof(out));
    assert(read_full(fd, in, sizeof(in), 0) == (int)sizeof(in));
    assert(memcmp(out, in, sizeof(out)) == 0);

    close_scratch(fd);

    printf("test_write_then_read_back: passed\n");
}

// a large buffer at an unaligned, nonzero offset comes back byte for byte
void test_large_round_trip_at_offset(void)
{
    int fd = open_scratch();

    size_t n = 4 * 1024 * 1024;
    off_t offset = 3;

    uint8_t *out = malloc(n);
    uint8_t *in = malloc(n);
    assert(out != NULL && in != NULL);

    fill_pattern(out, n);

    assert(write_full(fd, out, n, offset) == (int)n);
    assert(read_full(fd, in, n, offset) == (int)n);
    assert(memcmp(out, in, n) == 0);

    // the 3 bytes before the offset were never written: a hole of zeros
    uint8_t head[3] = {0xFF, 0xFF, 0xFF};
    assert(read_full(fd, head, sizeof(head), 0) == 3);
    assert(head[0] == 0 && head[1] == 0 && head[2] == 0);

    free(out);
    free(in);
    close_scratch(fd);

    printf("test_large_round_trip_at_offset: passed\n");
}

// writing at an offset must not disturb the bytes around it
void test_write_at_offset_leaves_neighbours(void)
{
    int fd = open_scratch();

    char base[] = "aaaaaaaaaa";
    assert(write_full(fd, base, 10, 0) == 10);

    assert(write_full(fd, "XYZ", 3, 4) == 3);

    char in[10];
    assert(read_full(fd, in, 10, 0) == 10);
    assert(memcmp(in, "aaaaXYZaaa", 10) == 0);

    close_scratch(fd);

    printf("test_write_at_offset_leaves_neighbours: passed\n");
}

// reading past the end is not an error: it returns the bytes that exist
void test_read_short_at_eof(void)
{
    int fd = open_scratch();

    assert(write_full(fd, "12345", 5, 0) == 5);

    char in[16];
    memset(in, 'z', sizeof(in));

    assert(read_full(fd, in, sizeof(in), 0) == 5);
    assert(memcmp(in, "12345", 5) == 0);

    // partway into the file
    assert(read_full(fd, in, sizeof(in), 3) == 2);
    assert(memcmp(in, "45", 2) == 0);

    close_scratch(fd);

    printf("test_read_short_at_eof: passed\n");
}

void test_read_at_or_past_eof_returns_zero(void)
{
    int fd = open_scratch();

    assert(write_full(fd, "12345", 5, 0) == 5);

    char in[4];

    assert(read_full(fd, in, sizeof(in), 5) == 0);    // exactly at EOF
    assert(read_full(fd, in, sizeof(in), 100) == 0);  // well past EOF

    close_scratch(fd);

    printf("test_read_at_or_past_eof_returns_zero: passed\n");
}

void test_zero_length(void)
{
    int fd = open_scratch();

    char b = 'x';

    assert(write_full(fd, &b, 0, 0) == 0);
    assert(read_full(fd, &b, 0, 0) == 0);
    assert(b == 'x');

    close_scratch(fd);

    printf("test_zero_length: passed\n");
}

// real errors are -1 with the real errno, not a made-up one
void test_bad_fd(void)
{
    char b[4] = {0};

    errno = 0;
    assert(read_full(-1, b, sizeof(b), 0) == -1);
    assert(errno == EBADF);

    errno = 0;
    assert(write_full(-1, b, sizeof(b), 0) == -1);
    assert(errno == EBADF);

    printf("test_bad_fd: passed\n");
}

void test_write_to_read_only_fd(void)
{
    int fd = open_scratch();
    assert(write_full(fd, "abc", 3, 0) == 3);
    close(fd);

    fd = open(SCRATCH, O_RDONLY);
    assert(fd != -1);

    errno = 0;
    assert(write_full(fd, "xyz", 3, 0) == -1);
    assert(errno == EBADF);

    close_scratch(fd);

    printf("test_write_to_read_only_fd: passed\n");
}

// /dev/full accepts the open but every write fails with ENOSPC
void test_write_to_full_device(void)
{
    int fd = open("/dev/full", O_WRONLY);
    assert(fd != -1);

    errno = 0;
    assert(write_full(fd, "abc", 3, 0) == -1);
    assert(errno == ENOSPC);

    close(fd);

    printf("test_write_to_full_device: passed\n");
}

// the kernel does a short write at the file size limit, then fails the
// next call with EFBIG: write_full must report the failure, and the bytes
// that did fit must be on disk
void test_short_write_then_error(void)
{
    int fd = open_scratch();

    struct rlimit old, lim;
    assert(getrlimit(RLIMIT_FSIZE, &old) == 0);

    // without this the process is killed by SIGXFSZ instead of getting EFBIG
    signal(SIGXFSZ, SIG_IGN);

    lim = old;
    lim.rlim_cur = 1000;
    assert(setrlimit(RLIMIT_FSIZE, &lim) == 0);

    size_t n = 4096;
    uint8_t *out = malloc(n);
    assert(out != NULL);
    fill_pattern(out, n);

    errno = 0;
    int r = write_full(fd, out, n, 0);
    int err = errno;

    assert(setrlimit(RLIMIT_FSIZE, &old) == 0);

    assert(r == -1);
    assert(err == EFBIG);

    struct stat st;
    assert(fstat(fd, &st) == 0);
    assert(st.st_size == 1000);

    uint8_t in[1000];
    assert(read_full(fd, in, sizeof(in), 0) == 1000);
    assert(memcmp(in, out, 1000) == 0);

    free(out);
    close_scratch(fd);

    printf("test_short_write_then_error: passed\n");
}

int main(void)
{
    test_write_then_read_back();
    test_large_round_trip_at_offset();
    test_write_at_offset_leaves_neighbours();
    test_read_short_at_eof();
    test_read_at_or_past_eof_returns_zero();
    test_zero_length();
    test_bad_fd();
    test_write_to_read_only_fd();
    test_write_to_full_device();
    test_short_write_then_error();
}
