/* main.c源码 */
#define _XOPEN_SOURCE 700

#define FUSE_USE_VERSION 26
#include <stdio.h>
#include <fuse.h>
#include "../include/ddriver.h"
#include <linux/fs.h>
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define DEMO_DEFAULT_PERM        0777


/* 超级块 */
struct demo_super
{
    int     driver_fd;  /* 模拟磁盘的fd */

    int     sz_io;      /* 磁盘IO大小，单位B */
    int     sz_disk;    /* 磁盘容量大小，单位B */
    int     sz_blks;    /* 逻辑块大小，单位B */
};

/* 目录项 */
struct demo_dentry
{
    char    fname[128];
}; 

struct demo_super super;

#define DEVICE_NAME "ddriver"

/* 挂载文件系统 */
static int demo_mount(struct fuse_conn_info * conn_info){
    // 打开驱动
    char device_path[128] = {0};
    sprintf(device_path, "%s/" DEVICE_NAME, getpwuid(getuid())->pw_dir);
    super.driver_fd = ddriver_open(device_path);

    printf("super.driver_fd: %d\n", super.driver_fd);


    /* 填充super信息 */
    int ret;
    if (ddriver_ioctl(super.driver_fd, IOC_REQ_DEVICE_IO_SZ, &ret) != 0 || ret <= 0)
        goto error_sz_io;
    super.sz_io = ret;

    if (ddriver_ioctl(super.driver_fd, IOC_REQ_DEVICE_SIZE, &ret) != 0)
        goto error_sz_disk;
    super.sz_disk = ret;

    super.sz_blks = super.sz_disk / super.sz_io; 

    return 0;

error_sz_disk:
    printf("Error after fetching `super.sz_disk`.\n");
    super.sz_io = 0;
error_sz_io:
    printf("Error after fetching `super.sz_io`.\n");
    return -EIO;
}

/* 卸载文件系统 */
static int demo_umount(void* p){
    // 关闭驱动
    ddriver_close(super.driver_fd);
}

/* 遍历目录 */
static int demo_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
    // 此处任务一同学无需关注demo_readdir的传入参数，也不要使用到上述参数

    char filename[128]; // 待填充的

    /* 根据超级块的信息，从第500逻辑块读取一个dentry，ls将只固定显示这个文件名 */

    /* TODO: 计算磁盘偏移off，并根据磁盘偏移off调用ddriver_seek移动磁盘头到磁盘偏移off处 */
    off_t off = 500 * 2 * super.sz_io;
    printf("off=%llu, super.sz_io=%d, super.sz_disk=%d\n", off, super.sz_io, super.sz_disk);
    if (off >= super.sz_disk)
        goto error_off;
    if (ddriver_seek(super.driver_fd, off, SEEK_SET) != off)
        goto error_seek;
    /* TODO: 调用ddriver_read读出一个磁盘块到内存，512B */
    char *block_buf = malloc(super.sz_io);
    if (block_buf == NULL)
        goto error_malloc_block_buf;
    if (ddriver_read(super.driver_fd, block_buf, super.sz_io) != super.sz_io) 
        goto error_read;
    /* TODO: 使用memcpy拷贝上述512B的前sizeof(demo_dentry)字节构建一个demo_dentry结构 */
    if (sizeof(struct demo_dentry) > super.sz_io)
        goto error_dentry_size;
    struct demo_dentry *dentry = malloc(sizeof(struct demo_dentry));
    if (dentry == NULL)
        goto error_malloc_dentry;
    memcpy((void *)dentry, (const void *)block_buf, sizeof(struct demo_dentry));
    /* TODO: 填充filename */
    memcpy((void *)filename, (const void *)&dentry->fname, 128);

    free(dentry);
    free(block_buf);
    // 此处大家先不关注filler，已经帮同学写好，同学填充好filename即可
    return filler(buf, filename, NULL, 0);

error_malloc_dentry:
    printf("Error after malloc `dentry`.\n");
error_dentry_size:
    printf("Error after comparing `sizeof(struct demo_dentry) > super.sz_io`.\n");
error_read:
    printf("Error after comparing `ddriver_read(...) == super.sz_io`.\n");
    free(block_buf);
error_malloc_block_buf:
    printf("Error after malloc `block_buf`.\n");
error_seek:
    printf("Error after comparing `ddriver_seek(...) == off`.\n");
error_off:
    printf("Error after comparing `off >= super.sz_disk`.\n");
    return -ENXIO;
}

/* 显示文件属性 */
static int demo_getattr(const char* path, struct stat *stbuf)
{
    if(strcmp(path, "/") == 0)
        stbuf->st_mode = DEMO_DEFAULT_PERM | S_IFDIR;            // 根目录是目录文件
    else
        stbuf->st_mode = DEMO_DEFAULT_PERM | S_IFREG; /* TODO: 显示为普通文件 */ // 该文件显示为普通文件
    return 0;
}

/* 根据任务1需求 只用实现前四个钩子函数即可完成ls操作 */
static struct fuse_operations ops = {
	.init = demo_mount,						          /* mount文件系统 */		
	.destroy = demo_umount,							  /* umount文件系统 */
	.getattr = demo_getattr,							  /* 获取文件属性 */
	.readdir = demo_readdir,							  /* 填充dentrys */
};

int main(int argc, char *argv[])
{
    int ret = 0;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    ret = fuse_main(argc, argv, &ops, NULL);
    return ret;
}
