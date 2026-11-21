#include <unistd.h>
#include <errno.h>
#include <stddef.h>

#include "io.h"

ssize_t read_full(int fd, void *buf, size_t n, off_t offset)
{
    size_t rn = 0;
    ssize_t r = 0;

    do
    {
        r = pread(fd, (char *)buf + rn, n - rn, offset + rn);
        if (r > 0)
            rn += r;
    } while ((r < 0 && errno == EINTR) || (rn < n && r > 0));

    if (r < 0)
        return r;

    return rn;
}

ssize_t write_full(int fd, const void *buf, size_t n, off_t offset)
{
    size_t wn = 0;
    ssize_t w = 0;

    do
    {
        w = pwrite(fd, (const char *)buf + wn, n - wn, offset + wn);

        if (w > 0)
            wn += w;

    } while ((w < 0 && errno == EINTR) || (w > 0 && wn < n));

    if (n > 0 && w == 0)
    {
        errno = ENOSPC;
        return -1;
    }

    if (w < 0)
        return w;

    return wn;
}