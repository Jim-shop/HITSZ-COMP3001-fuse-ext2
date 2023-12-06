#define _XOPEN_SOURCE 700

#define FUSE_USE_VERSION 26

#pragma region "头文件"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include <fuse.h>
#include "driver.h"
#include "main.h"
#pragma endregion

#pragma region "宏定义"
#define FS001_MAGIC 0x01919810
#define FS001_SZ_BLK 1024
#define FS001_MAX_NAME_LEN 32
#define FS001_DATA_PER_INODE 6
#define FS001_DEFAULT_PERM 0777

#define FS001_ROOT_INO 0

#define FS001_MAX_INO 128
#define FS001_MAX_DNO 2048

#define FS001_SUPER_OFF (0 * FS001_SZ_BLK)
#define FS001_BITMAP_INODE_OFF (1 * FS001_SZ_BLK)
#define FS001_BITMAP_DATA_OFF (2 * FS001_SZ_BLK)
#define FS001_INODE_OFF(no) (3 * FS001_SZ_BLK + (no) * sizeof(struct fs001_inode))
#define FS001_DATA_OFF(no) ((10 - 1 + (no)) * FS001_SZ_BLK) // DNO start from 1
#pragma endregion

#pragma region "结构体"
enum fs001_file_type
{
    FS001_FILE,
    FS001_DIR,
};
typedef uint16_t fs001_file_type_t;
struct fs001_super
{
    struct driver *driver;
    uint16_t blk_disk;

    struct fs001_dentry *root_dentry;
    struct fs001_inode *root_inode;

    uint8_t bitmap_inode[FS001_SZ_BLK];
    uint8_t bitmap_data[FS001_SZ_BLK];

    struct fs001_super_d
    {
        uint32_t magic;
    } d;
};
struct fs001_inode
{
    struct fs001_dentry *dentry;
    struct fs001_dentry *dentries;
    uint8_t *data;

    struct fs001_inode_d
    {
        uint16_t ino;
        uint16_t size;
        fs001_file_type_t type;
        uint16_t dno[FS001_DATA_PER_INODE];
    } d;
};
struct fs001_dentry
{
    struct fs001_dentry *parent;
    struct fs001_dentry *next;
    struct fs001_inode *inode;

    struct fs001_dentry_d
    {
        uint16_t ino;
        fs001_file_type_t type;
        char name[FS001_MAX_NAME_LEN];
    } d;
};
#pragma endregion

#pragma region "全局变量"
struct fs001_super fs001_super;
#pragma endregion

#pragma region "dentry"
static struct fs001_dentry *fs001_new_dentry(char *fname, fs001_file_type_t type)
{
    struct fs001_dentry *dentry = malloc(sizeof(struct fs001_dentry));
    memset(dentry, 0, sizeof(struct fs001_dentry));
    memcpy(dentry->d.name, fname, strlen(fname));
    dentry->d.type = type;
    dentry->d.ino = -1;
    dentry->inode = NULL;
    dentry->parent = NULL;
    dentry->next = NULL;
    return dentry;
}
static int fs001_append_dentry(struct fs001_inode *inode, struct fs001_dentry *dentry)
{
    dentry->next = inode->dentries; // NULLable
    inode->dentries = dentry;
    inode->d.size += sizeof(struct fs001_dentry_d);
    return 0;
}
static struct fs001_dentry *fs001_get_dentry(struct fs001_inode *inode, int dir)
{
    struct fs001_dentry *dentry_cursor = inode->dentries;
    while (dentry_cursor != NULL)
    {
        if (dir-- == 0)
            return dentry_cursor;
        dentry_cursor = dentry_cursor->next;
    }
    return NULL;
}
#pragma endregion

#pragma region "data"
static uint16_t fs001_alloc_data(void)
{
    uint16_t dno;
    for (dno = 1; dno < FS001_MAX_DNO; dno++)
    {
        if (fs001_super.bitmap_data[dno / 8] & (1 << (dno % 8))) // used
            continue;
        fs001_super.bitmap_data[dno / 8] |= 1 << (dno % 8); // set used
        break;
    }
    if (dno >= FS001_MAX_DNO)
        goto error_bitmap;
    return dno;

error_bitmap:
    return 0;
}
static void fs001_free_data(uint16_t dno)
{
    if (dno != 0)
        fs001_super.bitmap_data[dno / 8] &= ~(1 << (dno % 8));
}
static int fs001_write_data(struct fs001_inode *inode)
{
    uint16_t size = inode->d.size;
    if (size > FS001_DATA_PER_INODE * FS001_SZ_BLK)
        goto error_size_too_big;
    uint16_t i = 0;
    while (size > 0)
    {
        if (inode->d.dno[i] == 0)
            if ((inode->d.dno[i] = fs001_alloc_data()) == 0)
                goto error_alloc_data;
        int write_size = FS001_SZ_BLK > size ? size : FS001_SZ_BLK;
        driver_write(fs001_super.driver, (char *)inode->data, FS001_DATA_OFF(inode->d.dno[i]), write_size);
        size -= write_size;
        i++;
    }
    while (i < FS001_DATA_PER_INODE)
    {
        fs001_free_data(inode->d.dno[i]);
        inode->d.dno[i] = 0;
        i++;
    }
    return 0;

error_alloc_data:
error_size_too_big:
    return -1;
}
static int fs001_read_data(struct fs001_inode *inode)
{
    uint16_t size = inode->d.size;
    if (size > FS001_DATA_PER_INODE * FS001_SZ_BLK)
        goto error_size_too_big;
    if (size != 0 && (inode->data = malloc(size)) == NULL)
        goto error_malloc;
    uint16_t i = 0;
    while (size > 0)
    {
        if (inode->d.dno[i] == 0)
            goto error_no_data;
        int read_size = FS001_SZ_BLK > size ? size : FS001_SZ_BLK;
        driver_read(fs001_super.driver, inode->data, FS001_DATA_OFF(inode->d.dno[i]), read_size);
        size -= read_size;
        i++;
    }
    while (i < FS001_DATA_PER_INODE)
    {
        fs001_free_data(inode->d.dno[i]);
        inode->d.dno[i] = 0;
        i++;
    }
    return 0;

error_no_data:
    free(inode->data);
error_malloc:
error_size_too_big:
    return -1;
}
#pragma endregion

#pragma region "inode"
static struct fs001_inode *fs001_alloc_inode(struct fs001_dentry *dentry)
{
    /* 1) find free ino */
    int ino = 0;
    for (; ino < sizeof(fs001_super.bitmap_inode) * 8; ino++)
    {
        if (fs001_super.bitmap_inode[ino / 8] & (1 << (ino % 8))) // used
            continue;
        fs001_super.bitmap_inode[ino / 8] |= 1 << (ino % 8); // set used
        break;
    }
    if (ino >= FS001_MAX_INO)
        goto error_bitmap;
    /* 2) alloc */
    struct fs001_inode *inode = malloc(sizeof(struct fs001_inode));
    if (inode == NULL)
        goto error_malloc;
    inode->d.ino = ino;
    inode->d.size = 0;
    inode->dentry = dentry;
    inode->dentries = NULL;
    inode->data = NULL;
    inode->d.type = dentry->d.type;
    memset(&inode->d.dno, 0, sizeof(inode->d.dno));
    /* 3) connect */
    dentry->d.ino = ino;
    dentry->inode = inode;

    return inode;

error_malloc:
    fs001_super.bitmap_inode[ino / 8] &= ~(1 << (ino % 8));
error_bitmap:
    return NULL;
}
static struct fs001_inode *fs001_read_inode(struct fs001_dentry *dentry, uint16_t ino)
{
    /* 1) alloc mem space */
    struct fs001_inode *inode = malloc(sizeof(struct fs001_inode));
    if (inode == NULL)
        goto error_malloc;
    /* 2) read inode from disk */
    if (driver_read(fs001_super.driver, &inode->d, FS001_INODE_OFF(ino), sizeof(struct fs001_inode_d)) != 0)
        goto error_read;
    inode->data = NULL;
    inode->dentry = dentry;
    inode->dentries = NULL;
    dentry->inode = inode;
    /* 3) read content */
    if (fs001_read_data(inode) != 0)
        goto error_read_data;
    if (inode->d.type == FS001_DIR)
    {
        for (int i = 0; i < inode->d.size; i += sizeof(struct fs001_dentry_d))
        {
            struct fs001_dentry_d *dentry_d = (struct fs001_dentry_d *)(inode->data + i);
            struct fs001_dentry *sub_dentry = fs001_new_dentry(dentry_d->name, dentry_d->type);
            sub_dentry->parent = inode->dentry;
            sub_dentry->d.ino = dentry_d->ino;
            fs001_append_dentry(inode, sub_dentry);
        }
    }
    return inode;

error_read_data:
error_read:
    free(inode);
error_malloc:
    return NULL;
}
static int fs001_write_inode(struct fs001_inode *inode)
{
    /* 1) write data (dir, file, ...) */
    if (inode->d.type == FS001_DIR && inode->d.size != 0)
    {
        if (inode->data != NULL)
            free(inode->data);
        if ((inode->data = malloc(inode->d.size)) == NULL)
            goto error_malloc;
        struct fs001_dentry *dentry_cursor = inode->dentries;
        int offset = 0;
        while (dentry_cursor != NULL)
        {
            memcpy(inode->data + offset, &dentry_cursor->d, sizeof(struct fs001_dentry_d));
            if (dentry_cursor->inode != NULL)
                fs001_write_inode(dentry_cursor->inode);
            offset += sizeof(struct fs001_dentry_d);
            dentry_cursor = dentry_cursor->next;
        }
        fs001_write_data(inode);
    }
    else if (inode->d.type == FS001_FILE)
    {
        if (fs001_write_data(inode) != 0)
            goto error_write_data;
    }
    /* 2) write inode itself */
    if (driver_write(fs001_super.driver, (void *)&inode->d, FS001_INODE_OFF(inode->d.ino), sizeof(struct fs001_inode_d)) != 0)
        goto error_write;
    return 0;

error_malloc:
error_write_data:
error_write:
    return -EIO;
}
#pragma endregion

#pragma region "工具函数"
static char *fs001_get_fname(const char *path)
{
    char *last_slash = strrchr(path, '/');
    if (last_slash == NULL)
        return NULL;
    char *name = last_slash + 1;
    if (strlen(name) == 0)
        return NULL;
    return name;
}

static bool fs001_is_root(const char *path)
{
    return strcmp(path, "/") == 0;
}

static int fs001_calc_level(const char *path)
{
    if (fs001_is_root(path))
        return 0;
    int lvl = 0;
    while (*path != '\0')
    {
        if (*path == '/')
            lvl++;
        path++;
    }
    return lvl;
}

static struct fs001_dentry *fs001_lookup(const char *path, bool *is_find)
{
    int level = fs001_calc_level(path);
    if (level == 0)
    {
        if (is_find != NULL)
            *is_find = true;
        return fs001_super.root_dentry;
    }

    struct fs001_dentry *dentry_cursor = fs001_super.root_dentry;
    struct fs001_dentry *ret = NULL;

    char *path_cpy = strdup(path);
    char *fname = strtok(path_cpy, "/");
    while (fname != NULL)
    {
        level--;
        if (dentry_cursor->inode == NULL)
            if (fs001_read_inode(dentry_cursor, dentry_cursor->d.ino) == NULL)
                goto error_read_inode;

        struct fs001_inode *inode = dentry_cursor->inode;
        if (inode->d.type == FS001_FILE)
        {
            if (is_find != NULL)
                *is_find = (level == 0);
            ret = inode->dentry;
            break;
        }
        else if (inode->d.type == FS001_DIR)
        {
            bool is_hit = false;
            dentry_cursor = inode->dentries;
            while (dentry_cursor != NULL)
            {
                if (strcmp(dentry_cursor->d.name, fname) == 0)
                {
                    is_hit = true;
                    break;
                }
                dentry_cursor = dentry_cursor->next;
            }

            if (!is_hit)
            {
                if (is_find != NULL)
                    *is_find = false;
                ret = inode->dentry;
                break;
            }
            else if (is_hit && level == 0)
            {
                if (is_find != NULL)
                    *is_find = true;
                ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/");
    }
    if (ret->inode == NULL)
        ret->inode = fs001_read_inode(ret, ret->d.ino);
finish:
    free(path_cpy);
    return ret;

error_read_inode:
    free(path_cpy);
    return NULL;
}
#pragma endregion

#pragma region "fs001"
void *fs001_init(struct fuse_conn_info *conn_info)
{
    /* 1) init driver */
    if ((fs001_super.driver = driver_init((char *)fs001_options.device)) == NULL)
    {
        perror("fs001_init: driver_init");
        goto error_driver_init;
    }
    /* 2) get disk size */
    if ((fs001_super.blk_disk = driver_get_disk_size(fs001_super.driver) / FS001_SZ_BLK) == 0)
    {
        perror("fs001_init: blk_disk");
        goto error_blk_disk;
    }
    assert(fs001_super.blk_disk >= FS001_MAX_INO + (FS001_MAX_DNO - 1) + 3);
    /* 3) read super block */
    if (driver_read(fs001_super.driver, &fs001_super.d, FS001_SUPER_OFF, sizeof(struct fs001_super_d)) != 0)
    {
        perror("fs001_init: driver_read");
        goto error_driver_read;
    }
    /* 4) create root dentry */
    if ((fs001_super.root_dentry = fs001_new_dentry("/", FS001_DIR)) == NULL)
    {
        perror("fs001_init: new root_dentry");
        goto error_new_root_dentry;
    }
    /* 5) verify root or else init file system */
    if (fs001_super.d.magic != FS001_MAGIC)
    {
        /* 5.1) init file system */
        fs001_super.d.magic = FS001_MAGIC;
        assert(FS001_MAX_INO <= sizeof(fs001_super.bitmap_inode) * 8);
        memset(&fs001_super.bitmap_inode, 0, sizeof(fs001_super.bitmap_inode));
        /*
            for (int i = 0; i < sizeof(fs001_super.bitmap_inode) * 8; i++)
            {
                if (i < FS001_MAX_INO)
                    fs001_super.bitmap_inode[i / 8] &= ~(1 << (i % 8));
                else
                    fs001_super.bitmap_inode[i / 8] |= 1 << (i % 8);
            }
        */
        assert(FS001_MAX_DNO <= sizeof(fs001_super.bitmap_data) * 8);
        memset(&fs001_super.bitmap_data, 0, sizeof(fs001_super.bitmap_data));
        /*
            for (int i = 0; i < sizeof(fs001_super.bitmap_data) * 8; i++)
            {
                if (i != 0 && i < FS001_MAX_DNO)
                    fs001_super.bitmap_data[i / 8] &= ~(1 << (i % 8));
                else
                    fs001_super.bitmap_data[i / 8] |= 1 << (i % 8);
            }
        */
        /* 5.2) alloc root inode */
        fs001_alloc_inode(fs001_super.root_dentry);
    }
    else
    {
        /* 5.1) read bitmap_inode */
        if (driver_read(fs001_super.driver, fs001_super.bitmap_inode, FS001_BITMAP_INODE_OFF, sizeof(fs001_super.bitmap_inode)) != 0)
        {
            perror("fs001_init: ddriver_read");
            goto error_index_bitmap_ddriver_read;
        }
        /* 5.2) read bitmap_data */
        if (driver_read(fs001_super.driver, fs001_super.bitmap_data, FS001_BITMAP_DATA_OFF, sizeof(fs001_super.bitmap_data)) != 0)
        {
            perror("fs001_init: ddriver_read");
            goto error_data_bitmap_ddriver_read;
        }
        /* 5.3) read root inode */
        fs001_super.root_inode = fs001_read_inode(fs001_super.root_dentry, FS001_ROOT_INO);
        if (fs001_super.root_inode == NULL)
        {
            perror("fs001_init: read root_inode");
            goto error_read_root_inode;
        }
    }

    return NULL;

error_read_root_inode:
error_data_bitmap_ddriver_read:
error_index_bitmap_ddriver_read:
    free(fs001_super.root_dentry);
error_new_root_dentry:
error_driver_read:
error_blk_disk:
    driver_close(fs001_super.driver);
error_driver_init:
    fuse_exit(fuse_get_context()->fuse);
    return (void *)-EINVAL;
}

void fs001_destroy(void *p)
{
    /* 1) forbid reentrance */
    if (fs001_super.driver == NULL)
        return;
    /* 2) write inodes */
    if (fs001_write_inode(fs001_super.root_dentry->inode) != 0)
        perror("fs001_destroy: write inode");
    /* 3) write super block */
    if (driver_write(fs001_super.driver, (char *)&fs001_super.d, FS001_SUPER_OFF, sizeof(struct fs001_super_d)) != 0)
        perror("fs001_destroy: write super");
    /* 4) write bitmap */
    if ((driver_write(fs001_super.driver, (char *)fs001_super.bitmap_inode, FS001_BITMAP_INODE_OFF, sizeof(fs001_super.bitmap_inode))) != 0)
        perror("fs001_destroy: write bitmap inode");
    if ((driver_write(fs001_super.driver, (char *)fs001_super.bitmap_data, FS001_BITMAP_DATA_OFF, sizeof(fs001_super.bitmap_data))) != 0)
        perror("fs001_destroy: write bitmap data");
    /* 5) release driver */
    driver_close(fs001_super.driver);
    fs001_super.driver = NULL;

    return;
}

int fs001_mkdir(const char *path, mode_t mode)
{
    bool is_find;
    struct fs001_dentry *last_dentry = fs001_lookup(path, &is_find);
    if (is_find)
        return -EEXIST;

    if (last_dentry->inode->d.type == FS001_FILE)
        return -ENXIO;

    char *fname = fs001_get_fname(path);
    struct fs001_dentry *dentry = fs001_new_dentry(fname, FS001_DIR);
    dentry->parent = last_dentry;
    fs001_alloc_inode(dentry);
    fs001_append_dentry(last_dentry->inode, dentry);

    return 0;
}

int fs001_getattr(const char *path, struct stat *fs001_stat)
{
    bool is_find;
    struct fs001_dentry *dentry = fs001_lookup(path, &is_find);
    if (!is_find)
        return -ENOENT;

    fs001_stat->st_size = dentry->inode->d.size;
    fs001_stat->st_nlink = 1;
    fs001_stat->st_uid = getuid();
    fs001_stat->st_gid = getgid();
    fs001_stat->st_atime = time(NULL);
    fs001_stat->st_mtime = time(NULL);
    fs001_stat->st_blksize = FS001_SZ_BLK;
    if (dentry->inode->d.type == FS001_DIR)
        fs001_stat->st_mode = S_IFDIR | FS001_DEFAULT_PERM;
    else if (dentry->inode->d.type == FS001_FILE)
        fs001_stat->st_mode = S_IFREG | FS001_DEFAULT_PERM;
    if (fs001_is_root(path))
    {
        fs001_stat->st_size = 0;
        fs001_stat->st_blocks = fs001_super.blk_disk;
        fs001_stat->st_nlink = 2; /* !特殊，根目录link数为2 */
    }
    return 0;
}

int fs001_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    bool is_find;
    struct fs001_dentry *dentry = fs001_lookup(path, &is_find);

    if (!is_find)
        return -ENOENT;

    struct fs001_dentry *sub_dentry = fs001_get_dentry(dentry->inode, offset);
    if (sub_dentry == NULL)
        return -EIO;

    filler(buf, sub_dentry->d.name, NULL, offset + 1);

    return 0;
}

int fs001_mknod(const char *path, mode_t mode, dev_t dev)
{
    bool is_find;
    struct fs001_dentry *last_dentry = fs001_lookup(path, &is_find);

    if (is_find)
        return -EEXIST;

    char *fname = fs001_get_fname(path);
    struct fs001_dentry *dentry;
    if (S_ISREG(mode))
        dentry = fs001_new_dentry(fname, FS001_FILE);
    else if (S_ISDIR(mode))
        dentry = fs001_new_dentry(fname, FS001_DIR);
    else
        dentry = fs001_new_dentry(fname, FS001_FILE);

    dentry->parent = last_dentry;
    fs001_alloc_inode(dentry);
    fs001_append_dentry(last_dentry->inode, dentry);

    return 0;
}

int fs001_utimens(const char *path, const struct timespec tv[2])
{
    return 0;
}
#pragma endregion
