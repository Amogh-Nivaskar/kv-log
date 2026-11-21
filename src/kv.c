#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdbool.h>

#include "hashmap.h"
#include "crc.h"
#include "bytes.h"
#include "io.h"
#include "kv.h"

#define HEADER_SIZE 21

typedef struct
{
    uint32_t crc;
    uint64_t timestamp;
    uint32_t key_len;
    uint32_t val_len;
    uint8_t flags;
} header_t;

typedef struct
{
    off_t offset; // where the record starts in the log
    size_t size;  // total record length (header + key + value)
} loc_t;

off_t write_offset = 0;
hashmap_t map;
static int log_fd = -1;
static sync_mode_t sync_mode;
static uint64_t last_sync = 0;
static off_t sync_offset;
static uint64_t sync_interval;
bool sync_failed = false;

static void encode_header(uint8_t *out, const header_t *h)
{
    uint8_t *p = out;

    put_u32(p, h->crc);
    p += sizeof(uint32_t);
    put_u64(p, h->timestamp);
    p += sizeof(uint64_t);
    put_u32(p, h->key_len);
    p += sizeof(uint32_t);
    put_u32(p, h->val_len);
    p += sizeof(uint32_t);
    *p = h->flags;
    p += sizeof(uint8_t);

    assert(p - out == HEADER_SIZE);
}

static void decode_header(const uint8_t *in, header_t *h)
{
    const uint8_t *p = in;

    h->crc = get_u32(p);
    p += sizeof(uint32_t);
    h->timestamp = get_u64(p);
    p += sizeof(uint64_t);
    h->key_len = get_u32(p);
    p += sizeof(uint32_t);
    h->val_len = get_u32(p);
    p += sizeof(uint32_t);
    h->flags = *p;
    p += sizeof(uint8_t);

    assert(p - in == HEADER_SIZE);
}

static uint64_t get_timestamp_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int should_sync(sync_mode_t mode, uint64_t now, uint64_t last_sync_time, uint64_t interval)
{
    if (mode == SYNC_ALWAYS)
        return 1;

    if (mode == SYNC_INTERVAL)
        return now - last_sync_time >= interval;

    return 0;
}

static int maybe_sync(off_t end_offset)
{
    uint64_t curr_timestamp = get_timestamp_ms();

    if (should_sync(sync_mode, curr_timestamp, last_sync, sync_interval))
    {
        if (fdatasync(log_fd) == -1)
        {
            perror("fdatasync");
            return 0;
        }
        last_sync = curr_timestamp;
        sync_offset = end_offset;
    }
    return 1;
}

int bootup(sync_mode_t mode, int interval_ms)
{
    if (mode == SYNC_INTERVAL && interval_ms <= 0)
    {
        fprintf(stderr, "bootup: SYNC_INTERVAL needs interval_ms > 0\n");
        return 0;
    }

    sync_mode = mode;
    sync_interval = interval_ms;
    sync_failed = false;

    log_fd = open("data.log", O_RDWR | O_CREAT, 0644);

    if (log_fd == -1)
    {
        perror("open");
        return 0;
    }

    hashmap_init(&map, free);

    struct stat st;

    if (fstat(log_fd, &st) == -1)
    {
        perror("fstat");
        goto fail;
    }

    off_t file_size = st.st_size;

    off_t offset = 0;
    uint8_t rawhdr[HEADER_SIZE];
    header_t hdr;

    while (1)
    {
        ssize_t n = read_full(log_fd, rawhdr, HEADER_SIZE, offset);

        if (n < 0)
        {
            perror("read_full - header");
            goto fail;
        }

        if ((size_t)n < HEADER_SIZE)
        {
            if (ftruncate(log_fd, offset) == -1)
                perror("ftruncate");
            break;
        }

        decode_header(rawhdr, &hdr);

        size_t rec_size = HEADER_SIZE + hdr.key_len + hdr.val_len;

        if (rec_size > (size_t)file_size - offset)
        {
            if (ftruncate(log_fd, offset) == -1)
                perror("ftruncate");
            break;
        }

        uint8_t *buf = malloc(rec_size);

        if (buf == NULL)
        {
            perror("malloc - buf");
            goto fail;
        }

        n = read_full(log_fd, buf, rec_size, offset);

        if (n < 0)
        {
            free(buf);
            perror("read_full - record");
            goto fail;
        }
        if ((size_t)n < rec_size)
        {
            free(buf);
            if (ftruncate(log_fd, offset) == -1)
                perror("ftruncate");
            break;
        }

        if (hdr.crc != crc32(buf + sizeof(hdr.crc), rec_size - sizeof(hdr.crc)))
        {
            if (offset + (off_t)rec_size == file_size)
            {
                free(buf);
                if (ftruncate(log_fd, offset) == -1)
                    perror("ftruncate");
                break;
            }

            fprintf(stderr, "corrupt record at offset %lld\n", (long long)offset);
            free(buf);
            goto fail;
        }

        char *key = malloc(hdr.key_len + 1);

        if (key == NULL)
        {
            perror("malloc - key");
            free(buf);
            goto fail;
        }

        memcpy(key, buf + HEADER_SIZE, hdr.key_len);
        key[hdr.key_len] = '\0';

        if (hdr.flags == 0)
        {
            int r = hashmap_delete(&map, key);
            if (r == 0)
            {
                fprintf(stderr, "hashmap_delete failed");
                free(buf);
                free(key);
                goto fail;
            }
        }
        else
        {
            loc_t *loc = malloc(sizeof(loc_t));

            if (loc == NULL)
            {
                perror("malloc - loc");
                free(buf);
                free(key);
                goto fail;
            }

            loc->offset = offset;
            loc->size = rec_size;
            int r = hashmap_add(&map, key, loc);

            if (r == 0)
            {
                fprintf(stderr, "hashmap_add failed");
                free(buf);
                free(key);
                free(loc);
                goto fail;
            }
        }

        free(key);
        free(buf);

        offset += rec_size;
    }

    write_offset = offset;
    sync_offset = write_offset;
    last_sync = get_timestamp_ms();

    return 1;

fail:
    if (log_fd != -1)
    {
        close(log_fd);
        log_fd = -1;
    }
    hashmap_destroy(&map);
    return 0;
}

int put(char *key, char *value)
{
    if (sync_failed == true)
        return 0;

    header_t hdr = {
        .crc = 0,
        .flags = 1,
        .timestamp = time(NULL),
        .key_len = strlen(key),
        .val_len = strlen(value)};

    int rec_size = HEADER_SIZE + hdr.key_len + hdr.val_len;

    uint8_t *buf = malloc(rec_size);

    if (buf == NULL)
    {
        perror("malloc - buf");
        return 0;
    }

    encode_header(buf, &hdr);
    memcpy(buf + HEADER_SIZE, key, hdr.key_len);
    memcpy(buf + HEADER_SIZE + hdr.key_len, value, hdr.val_len);

    hdr.crc = crc32(buf + sizeof(hdr.crc), rec_size - sizeof(hdr.crc));
    put_u32(buf, hdr.crc);

    if (write_full(log_fd, buf, rec_size, write_offset) < 0)
    {
        perror("write_full");
        free(buf);

        // drop any half-written record so the log still ends on a record boundary
        if (ftruncate(log_fd, write_offset) == -1)
            perror("ftruncate");

        return 0;
    }

    if (maybe_sync(write_offset + rec_size) == 0)
    {
        fprintf(stderr, "maybe_sync failed\n");
        free(buf);
        sync_failed = true;
        return 0;
    }

    loc_t *loc = malloc(sizeof(loc_t));

    if (loc == NULL)
    {
        perror("malloc - loc");
        free(buf);
        free(loc);
        return 0;
    }

    loc->offset = write_offset;
    loc->size = rec_size;

    int r = hashmap_add(&map, key, loc);
    if (r == 0)
    {
        fprintf(stderr, "hashmap_add failed");
        free(buf);
        free(loc);
        return 0;
    }

    write_offset += rec_size;
    free(buf);

    return 1;
}

int get(char *key, char **value)
{
    void *rawloc;

    int found = hashmap_get(&map, key, &rawloc);

    if (found == 0)
        return 0;

    loc_t *loc = rawloc;

    int val_offset = HEADER_SIZE + strlen(key);

    uint8_t *rcd = malloc(loc->size);

    if (rcd == NULL)
    {
        perror("malloc - rcd");
        return 0;
    }

    ssize_t n = read_full(log_fd, rcd, loc->size, loc->offset);

    if (n < 0)
    {
        perror("read_full");
        free(rcd);
        return 0;
    }

    if ((size_t)n < loc->size)
    {
        free(rcd);
        return 0;
    }

    header_t hdr;
    decode_header(rcd, &hdr);

    if (hdr.crc != crc32(rcd + sizeof(hdr.crc), loc->size - sizeof(hdr.crc)))
    {
        fprintf(stderr, "corrupt record at offset %lld\n", (long long)loc->offset);
        free(rcd);
        return -1;
    }

    char *v = malloc(hdr.val_len + 1);

    if (v == NULL)
    {
        perror("malloc - v");
        free(rcd);
        return 0;
    }

    memcpy(v, rcd + val_offset, hdr.val_len);
    v[hdr.val_len] = '\0';

    *value = v;

    free(rcd);

    return 1;
}

int delete(char *key)
{
    if (sync_failed == true)
        return 0;

    header_t hdr = {
        .crc = 0,
        .flags = 0,
        .timestamp = time(NULL),
        .key_len = strlen(key),
        .val_len = 0};

    int rec_size = HEADER_SIZE + hdr.key_len + hdr.val_len;

    int r = hashmap_get(&map, key, NULL);

    if (r == 0)
    {
        return 1;
    }

    uint8_t *buf = malloc(rec_size);

    if (buf == NULL)
    {
        perror("malloc");
        return 0;
    }

    encode_header(buf, &hdr);
    memcpy(buf + HEADER_SIZE, key, hdr.key_len);

    hdr.crc = crc32(buf + sizeof(hdr.crc), rec_size - sizeof(hdr.crc));
    put_u32(buf, hdr.crc);

    if (write_full(log_fd, buf, rec_size, write_offset) < 0)
    {
        perror("write_full");
        free(buf);

        // drop any half-written record so the log still ends on a record boundary
        if (ftruncate(log_fd, write_offset) == -1)
            perror("ftruncate");

        return 0;
    }

    if (maybe_sync(write_offset + rec_size) == 0)
    {
        fprintf(stderr, "maybe_sync failed\n");
        free(buf);
        sync_failed = true;
        return 0;
    }

    r = hashmap_delete(&map, key);

    if (r == 0)
    {
        fprintf(stderr, "hashmap_delete failed");
        free(buf);
        return 0;
    }

    write_offset += rec_size;

    free(buf);

    return 1;
}

void shutdown()
{
    if (log_fd == -1)
        return;

    if (fdatasync(log_fd) == -1)
    {
        perror("fdatasync");
    }
    close(log_fd);
    log_fd = -1;

    hashmap_destroy(&map);
}

void reset()
{
    if (unlink("data.log") == -1 && errno != ENOENT)
    {
        perror("unlink");
        exit(1);
    }
    write_offset = 0;
    shutdown();
}
