#define _XOPEN_SOURCE 700

#define FUSE_USE_VERSION 26

#include <stddef.h>
#include <string.h>
#include "fs001.h"
#include "main.h"

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