struct driver *driver_init(char *device_path);
int driver_close(struct driver *driver);
int driver_get_disk_size(struct driver *driver);
int driver_read(struct driver *driver, void *target, int offset, int size);
int driver_write(struct driver *driver, char *source, size_t offset, size_t size);