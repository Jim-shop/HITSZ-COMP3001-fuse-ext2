#define _XOPEN_SOURCE 700

#define FUSE_USE_VERSION 26

#include <stddef.h>
#include <string.h>
#include "fs001.h"

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

struct custom_options
{
    const char *device;
};
struct custom_options fs001_options;

#define OPTION(t, p)                               \
    {                                              \
        (t), offsetof(struct custom_options, p), 1 \
    }

static const struct fuse_opt option_spec[] = {
    /* 用于FUSE文件系统解析参数 */
    OPTION("--device=%s", device),
    FUSE_OPT_END,
};

static struct fuse_operations operations = {
    .init = fs001_init,
    .destroy = fs001_destroy,
    .mkdir = fs001_mkdir,
    .getattr = fs001_getattr,
    .readdir = fs001_readdir,
    .mknod = fs001_mknod,
    .write = NULL,
    .read = NULL,
    .utimens = fs001_utimens,
    .truncate = NULL,
    .unlink = NULL,
    .rmdir = NULL,
    .rename = NULL,
    .open = fs001_open,
    .opendir = fs001_opendir,
    .access = NULL,
};

int main(int argc, char **argv)
{
    int ret;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    fs001_options.device = strdup("./hitsz"); // strdup("TODO: 这里填写你的ddriver设备路径");

    if (fuse_opt_parse(&args, &fs001_options, option_spec, NULL) == -1)
        return -1;

    ret = fuse_main(args.argc, args.argv, &operations, NULL);
    fuse_opt_free_args(&args);
    return ret;
}