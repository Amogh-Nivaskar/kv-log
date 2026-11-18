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

#include "hashmap.h"
#include "crc.h"

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

off_t write_offset = 0;

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

    struct stat st;

    if (fstat(log_fd, &st) == -1)
    {
        perror("fstat");
        return 0;
    }

    off_t file_size = st.st_size;

    off_t offset = 0;
    header_t hdr;

    hashmap_init(&map, free);

    while (1)
    {
        ssize_t n = pread(log_fd, &hdr, sizeof(header_t), offset);

        if (n < 0)
        {
            perror("pread");
            goto fail;
        }
        if ((size_t)n < sizeof(header_t))
        {
            if (ftruncate(log_fd, offset) == -1)
                perror("ftruncate");
            break;
        }

        size_t rec_size = sizeof(header_t) + hdr.key_len + hdr.val_len;

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

        memcpy(key, buf + sizeof(header_t), hdr.key_len);
        key[hdr.key_len] = '\0';

        if (hdr.is_deleted == true)
            hashmap_delete(&map, key);
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
            hashmap_add(&map, key, loc);
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
        .val_len = strlen(value),
        .is_deleted = false};

    int rec_size = sizeof(header_t) + hdr.key_len + hdr.val_len;

    uint8_t *buf = malloc(rec_size);

    if (buf == NULL)
    {
        perror("malloc - buf");
        return 0;
    }

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), key, hdr.key_len);
    memcpy(buf + sizeof(hdr) + hdr.key_len, value, hdr.val_len);

    hdr.crc = crc32(buf + sizeof(hdr.crc), rec_size - sizeof(hdr.crc));
    memcpy(buf, &hdr.crc, sizeof(hdr.crc));

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
        return 0;
    }

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

    if (rcd == NULL)
    {
        perror("malloc");
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

    header_t *hdr = (header_t *)rcd;

    if (hdr->crc != crc32(rcd + sizeof(hdr->crc), loc->size - sizeof(hdr->crc)))
    {
        fprintf(stderr, "corrupt record at offset %lld\n", (long long)loc->offset);
        free(rcd);
        return -1;
    }

    char *v = malloc(hdr->val_len + 1);
    memcpy(v, rcd + val_offset, hdr->val_len);
    v[hdr->val_len] = '\0';

    *value = v;

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

    if (buf == NULL)
    {
        perror("malloc");
        return 0;
    }

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), key, hdr.key_len);

    hdr.crc = crc32(buf + sizeof(hdr.crc), rec_size - sizeof(hdr.crc));
    memcpy(buf, &hdr.crc, sizeof(hdr.crc));

    int n = pwrite(log_fd, buf, rec_size, write_offset);

    if (n < rec_size)
    {
        free(buf);
        return 0;
    }

    hashmap_delete(&map, key);

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
