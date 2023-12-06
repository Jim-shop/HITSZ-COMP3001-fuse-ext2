#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "ddriver.h"

#define ROUND_DOWN(value, round) (((value) / (round)) * (round))
#define ROUND_UP(value, round) ((((value) + (round)-1) / (round)) * (round))

struct driver
{
    int fd;

    int sz_blk;
    int sz_disk;
    char *buf;
};

struct driver *driver_init(char *device_path)
{
    /* 1) alloc driver struct */
    struct driver *driver = malloc(sizeof(struct driver));
    if (driver == NULL)
        goto error_malloc;
    /* 2) open device: */
    if ((driver->fd = ddriver_open(device_path)) < 0)
        goto error_open;
    /* 3) verify blk size */
    if (ddriver_ioctl(driver->fd, IOC_REQ_DEVICE_IO_SZ, &driver->sz_blk) != 0 || driver->sz_blk <= 0)
        goto error_sz_blk;
    /* 4) alloc blk buffer */
    if ((driver->buf = malloc(driver->sz_blk)) == NULL)
        goto error_buf;
    /* 4) fetch and verify disk size */
    if (ddriver_ioctl(driver->fd, IOC_REQ_DEVICE_SIZE, &driver->sz_disk) != 0 || driver->sz_disk <= 0)
        goto error_blk_disk;

    return driver;

error_blk_disk:
    free(driver->buf);
    driver->buf = NULL;
error_buf:
error_sz_blk:
    ddriver_close(driver->fd);
    driver->fd = 0;
error_open:
    free(driver);
error_malloc:
    return NULL;
}

int driver_close(struct driver *driver)
{
    if (driver != NULL)
    {
        if (driver->buf != NULL)
        {
            free(driver->buf);
            driver->buf = NULL;
        }
        if (driver->fd != 0)
        {
            ddriver_close(driver->fd);
            driver->fd = 0;
        }
        free(driver);
    }
    return 0;
}

int driver_get_disk_size(struct driver *driver)
{
    return driver->sz_disk;
}

int driver_read(struct driver *driver, char *target, size_t offset, size_t size)
{
    size_t offset_aligned = ROUND_DOWN(offset, driver->sz_blk);
    size_t bias = offset - offset_aligned;

    if (ddriver_seek(driver->fd, offset_aligned, SEEK_SET) != offset_aligned)
        goto error_seek;
    if (bias != 0)
    { // unaligned start
        if (ddriver_read(driver->fd, driver->buf, driver->sz_blk) != driver->sz_blk)
            goto error_read;
        int read_size = driver->sz_blk - bias;
        read_size = read_size < size ? read_size : size;
        memcpy(target, driver->buf + bias, read_size);
        target += read_size;
        size -= read_size;
    }
    while (size >= driver->sz_blk)
    { // aligned middle
        if (ddriver_read(driver->fd, target, driver->sz_blk) != driver->sz_blk)
            goto error_read;
        target += driver->sz_blk;
        size -= driver->sz_blk;
    }
    if (size != 0)
    { // unaligned end
        if (ddriver_read(driver->fd, driver->buf, driver->sz_blk) != driver->sz_blk)
            goto error_read;
        memcpy(target, driver->buf, size);
    }

    return 0;

error_read:
error_seek:
    return -1;
}

int driver_write(struct driver *driver, char *source, size_t offset, size_t size)
{
    size_t offset_aligned = ROUND_DOWN(offset, driver->sz_blk);
    size_t bias = offset - offset_aligned;

    if (ddriver_seek(driver->fd, offset_aligned, SEEK_SET) != offset_aligned)
        goto error_seek;
    if (bias != 0)
    { // unaligned start
        if (ddriver_read(driver->fd, driver->buf, driver->sz_blk) != driver->sz_blk)
            goto error_read;
        int write_size = driver->sz_blk - bias;
        write_size = write_size < size ? write_size : size;
        memcpy(driver->buf + bias, source, write_size);
        if (ddriver_write(driver->fd, driver->buf, driver->sz_blk) != driver->sz_blk)
            goto error_write;
        source += write_size;
        size -= write_size;
    }
    while (size >= driver->sz_blk)
    { // aligned middle
        if (ddriver_write(driver->fd, source, driver->sz_blk) != driver->sz_blk)
            goto error_write;
        source += driver->sz_blk;
        size -= driver->sz_blk;
    }
    if (size != 0)
    { // unaligned end
        if (ddriver_read(driver->fd, driver->buf, driver->sz_blk) != driver->sz_blk)
            goto error_read;
        memcpy(driver->buf, source, size);
        if (ddriver_write(driver->fd, driver->buf, driver->sz_blk) != driver->sz_blk)
            goto error_write;
    }

    return 0;

error_write:
error_read:
error_seek:
    return -1;
}
