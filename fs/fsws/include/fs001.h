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
extern int fs001_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
extern int fs001_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
extern int fs001_unlink(const char *path);
extern int fs001_rmdir(const char *path);
extern int fs001_rename(const char *from, const char *to);
extern int fs001_open(const char *path, struct fuse_file_info *fi);
extern int fs001_opendir(const char *path, struct fuse_file_info *fi);
extern int fs001_truncate(const char *path, off_t offset);
extern int fs001_access(const char *path, int type);

#endif