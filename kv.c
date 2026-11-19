#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <assert.h>

#include "hashmap.h"
#include "crc.h"

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

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_u64(uint8_t *p, uint64_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
    p[4] = v >> 32;
    p[5] = v >> 40;
    p[6] = v >> 48;
    p[7] = v >> 56;
}

static uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)p[0] << 0 |
           (uint64_t)p[1] << 8 |
           (uint64_t)p[2] << 16 |
           (uint64_t)p[3] << 24 |
           (uint64_t)p[4] << 32 |
           (uint64_t)p[5] << 40 |
           (uint64_t)p[6] << 48 |
           (uint64_t)p[7] << 56;
}

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

int bootup()
{

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
        ssize_t n = pread(log_fd, rawhdr, HEADER_SIZE, offset);

        if (n < 0)
        {
            perror("pread");
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

        n = pread(log_fd, buf, rec_size, offset);

        if (n < 0)
        {
            free(buf);
            perror("pread");
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

    int n = pwrite(log_fd, buf, rec_size, write_offset);

    if (n < rec_size)
    {
        free(buf);
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

    ssize_t n = pread(log_fd, rcd, loc->size, loc->offset);

    if (n < 0)
    {
        perror("pread");
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

    int n = pwrite(log_fd, buf, rec_size, write_offset);

    if (n < rec_size)
    {
        free(buf);
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
    close(log_fd);
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
