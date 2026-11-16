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

#include "hashmap.h"

#define HEADER_SIZE 21

typedef struct
{
    uint32_t crc;
    time_t timestamp;
    uint32_t key_len;
    uint32_t val_len;
    uint8_t flags;
    bool is_deleted;
} header_t;

typedef struct
{
    off_t offset; // where the record starts in the log
    size_t size;  // total record length (header + key + value)
} loc_t;

int write_offset = 0;

hashmap_t map;
static int log_fd = -1;

int bootup()
{

    log_fd = open("data.log", O_RDWR | O_CREAT, 0644);

    if (log_fd == -1)
    {
        perror("open");
        return 0;
    }

    int offset = 0;
    header_t *hdr = malloc(sizeof(header_t));
    hashmap_init(&map, free);

    while (1)
    {
        size_t n = pread(log_fd, hdr, sizeof(header_t), offset);
        if (n < sizeof(header_t))
            break;

        size_t rec_size = sizeof(header_t) + hdr->key_len + hdr->val_len;

        loc_t *loc = malloc(sizeof(loc_t));

        loc->offset = offset;
        loc->size = rec_size;

        uint8_t *buf = malloc(rec_size);

        n = pread(log_fd, buf, rec_size, offset);

        if (n < rec_size)
        {
            free(buf);
            break;
        }

        char *key = malloc(hdr->key_len);
        memcpy(key, buf + sizeof(header_t), hdr->key_len);

        if (hdr->is_deleted == true)
        {
            hashmap_delete(&map, key);
        }
        else
        {
            hashmap_add(&map, key, loc);
        }

        offset += n;
    }

    free(hdr);
    write_offset = offset;

    return 1;
}

int put(char *key, char *value)
{
    header_t hdr = {
        .crc = 0,
        .flags = 1,
        .timestamp = time(NULL),
        .key_len = strlen(key),
        .val_len = strlen(value),
        .is_deleted = false};

    int rec_size = sizeof(header_t) + hdr.key_len + hdr.val_len;

    uint8_t *buf = malloc(rec_size);

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), key, hdr.key_len);
    memcpy(buf + sizeof(hdr) + hdr.key_len, value, hdr.val_len);

    int n = pwrite(log_fd, buf, rec_size, write_offset);

    if (n < rec_size)
        return 0;

    loc_t *loc = malloc(sizeof(loc_t));

    loc->offset = write_offset;
    loc->size = rec_size;

    hashmap_add(&map, key, loc);

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

    int val_offset = sizeof(header_t) + strlen(key);

    uint8_t *rcd = malloc(loc->size);

    ssize_t n = pread(log_fd, rcd, loc->size, loc->offset);

    if (n < 0)
    {
        perror("pread");
        return 0;
    }

    if ((size_t)n < loc->size)
    {
        return 0;
    }

    *value = strdup((char *)(rcd + val_offset));

    free(rcd);

    return 1;
}

int delete(char *key)
{
    header_t hdr = {
        .crc = 0,
        .flags = 1,
        .timestamp = time(NULL),
        .key_len = strlen(key),
        .val_len = 0,
        .is_deleted = true};

    int rec_size = sizeof(header_t) + hdr.key_len + hdr.val_len;

    uint8_t *buf = malloc(rec_size);

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), key, hdr.key_len);

    int n = pwrite(log_fd, buf, rec_size, write_offset);

    if (n < rec_size)
        return 0;

    hashmap_delete(&map, key);

    write_offset += rec_size;

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
