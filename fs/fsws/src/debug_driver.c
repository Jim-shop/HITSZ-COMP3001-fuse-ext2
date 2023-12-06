#include <stdio.h>

struct driver *driver_init(char *device_path)
{
    return (struct driver *)1;
}

int driver_close(struct driver *driver)
{
    return 0;
}

int driver_get_disk_size(struct driver *driver)
{
    return 4 * 1024 * 1024;
}

int driver_read(struct driver *driver, char *target, size_t offset, size_t size)
{
    FILE *f = fopen("dump.bin", "r");
    fseek(f, offset, SEEK_SET);
    fread(target, 1, size, f);
    fclose(f);
    return 0;
}

int driver_write(struct driver *driver, char *source, size_t offset, size_t size)
{
    FILE *f = fopen("dump.bin", "w");

    printf("f=%p, fseek=%d, offset=%lu\n", f, fseek(f, offset, SEEK_SET), offset);
    // fseek(f, offset, SEEK_SET);
    printf("fwrite=%lu\n", fwrite(source, 1, size, f));
    fclose(f);
    // printf("content:\n");
    // for (int i = 0; i < size; i++)
    //     printf("%x", source[i]);
    // printf("\n");
    return 0;
}
