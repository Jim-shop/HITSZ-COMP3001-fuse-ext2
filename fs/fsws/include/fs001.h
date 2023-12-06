#ifndef __FS001_H__
#define __FS001_H__

#include <fuse.h>

extern void *fs001_init(struct fuse_conn_info *conn_info);
extern void fs001_destroy(void *p);
extern int fs001_mkdir(const char *path, mode_t mode);
extern int fs001_getattr(const char *path, struct stat *fs001_stat);
extern int fs001_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
extern int fs001_mknod(const char *path, mode_t mode, dev_t dev);
extern int fs001_utimens(const char *path, const struct timespec tv[2]);

#endif