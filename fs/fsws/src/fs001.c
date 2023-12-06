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
#define FS001_MAX_NAME_LEN 128
#define FS001_DATA_PER_INODE 6
#define FS001_DEFAULT_PERM 0777

#define FS001_ROOT_INO 0

#define FS001_MAX_INO 1024
#define FS001_MAX_DNO 2048

#define FS001_SUPER_OFF (0 * FS001_SZ_BLK)
#define FS001_BITMAP_INODE_OFF (1 * FS001_SZ_BLK)
#define FS001_BITMAP_DATA_OFF (2 * FS001_SZ_BLK)
#define FS001_INODE_OFF(no) (FS001_BITMAP_DATA_OFF + (no) * sizeof(struct fs001_inode))
#define FS001_DATA_OFF(no) ((fs001_super.blk_disk - 1 - (no)) * FS001_SZ_BLK) // DNO start from 1
#pragma endregion

enum fs001_file_type
{
    FS001_FILE,
    FS001_DIR,
};

#define FS001_IS_DIR(pinode) (pinode->dentry->d.type == FS001_DIR)
#define FS001_IS_REG(pinode) (pinode->dentry->d.type == FS001_FILE)

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
        enum fs001_file_type type;
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
        enum fs001_file_type type;
        char name[FS001_MAX_NAME_LEN];
    } d;
};

struct fs001_super fs001_super;

#include "debug.h"

char *fs001_get_fname(const char *path)
{
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}

int fs001_calc_lvl(const char *path)
{
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char *str = path;
    int lvl = 0;
    if (strcmp(path, "/") == 0)
    {
        return lvl;
    }
    while (*str != NULL)
    {
        if (*str == '/')
        {
            lvl++;
        }
        str++;
    }
    return lvl;
}

/* DENTRY */
static struct fs001_dentry *fs001_new_dentry(char *fname, enum fs001_file_type type)
{
    struct fs001_dentry *dentry = malloc(sizeof(struct fs001_dentry));
    memset(dentry, 0, sizeof(struct fs001_dentry));
    memcpy(dentry->d.name, fname, strlen(fname));
    dentry->d.type = type;
    dentry->d.ino = -1;
    dentry->inode = NULL;
    dentry->parent = NULL;
    dentry->next = NULL;
}
int fs001_append_dentry(struct fs001_inode *inode, struct fs001_dentry *dentry)
{
    dentry->next = inode->dentries; // NULLable
    inode->dentries = dentry;
    inode->d.size += sizeof(struct fs001_dentry_d);
    return 0;
}
int fs001_drop_dentry(struct fs001_inode *inode, struct fs001_dentry *dentry)
{
    struct fs001_dentry *dentry_cursor = inode->dentries;
    bool is_find = false;
    if (dentry_cursor == dentry)
    {
        inode->dentries = dentry->next;
        is_find = true;
    }
    else
    {
        while (dentry_cursor != NULL)
        {
            if (dentry_cursor->next == dentry)
            {
                dentry_cursor->next = dentry->next;
                is_find = true;
                break;
            }
            dentry_cursor = dentry_cursor->next;
        }
    }
    if (!is_find)
        goto error_not_found;
    inode->d.size -= sizeof(struct fs001_dentry_d);
    return 0;

error_not_found:
    return -1;
}
struct fs001_dentry *fs001_get_dentry(struct fs001_inode *inode, int dir)
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

/* DATA */
uint16_t fs001_alloc_data(void)
{
    uint16_t dno;
    for (dno = 1; dno < FS001_MAX_DNO; dno++)
    {
        if (fs001_super.bitmap_data[dno / 8] & (1 << (dno % 8))) // used
            continue;
        fs001_super.bitmap_data[dno / 8] |= 1 << (dno % 8); // set used
        break;
    }
    if (dno == FS001_MAX_DNO)
        goto error_bitmap;
    return dno;

error_bitmap:
    return 0;
}
void fs001_free_data(uint16_t dno)
{
    if (dno != 0)
        fs001_super.bitmap_data[dno / 8] &= ~(1 << (dno % 8));
}
int fs001_write_data(struct fs001_inode *inode)
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
        driver_write(fs001_super.driver, inode->data, FS001_DATA_OFF(inode->d.dno[i]), write_size);
        size -= write_size;
        i++;
    }
    while (i < FS001_DATA_PER_INODE)
        fs001_free_data(inode->d.dno[i]);
    return 0;

error_alloc_data:
error_size_too_big:
    return -1;
}
int fs001_read_data(struct fs001_inode *inode)
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
        fs001_free_data(inode->d.dno[i]);
    return 0;

error_no_data:
    free(inode->data);
error_malloc:
error_size_too_big:
    return -1;
}

/* INODE */
struct fs001_inode *fs001_alloc_inode(struct fs001_dentry *dentry)
{
    /* 1) find free ino */
    int ino = 0;
    bool is_find_free_entry = false;
    for (; ino < sizeof(fs001_super.bitmap_inode) * 8; ino++)
    {
        if (fs001_super.bitmap_inode[ino / 8] & (1 << (ino % 8))) // used
            continue;
        is_find_free_entry = true;
        fs001_super.bitmap_inode[ino / 8] |= 1 << (ino % 8); // set used
        break;
    }
    if (!is_find_free_entry || ino == FS001_MAX_INO)
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
int fs001_free_inode(struct fs001_inode *inode)
{
    /* 1) check if is root */
    if (inode == fs001_super.root_dentry->inode)
        goto error_is_root;
    /* 2) recurse */
    if (inode->d.type == FS001_DIR)
    {
        struct fs001_dentry *dentry_cursor = inode->dentries;
        /* 递归向下drop */
        while (dentry_cursor)
        {
            fs001_free_inode(dentry_cursor->inode);
            fs001_drop_dentry(inode, dentry_cursor);
            struct fs001_dentry *dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->next;
            free(dentry_to_free);
        }
    }
    else if (inode->d.type == FS001_FILE)
    {
        fs001_super.bitmap_inode[inode->d.ino / 8] &= ~(1 << (inode->d.ino % 8));
        if (inode->data)
            free(inode->data);
        free(inode);
    }
    return 0;

error_is_root:
    return -1;
}
struct fs001_inode *fs001_read_inode(struct fs001_dentry *dentry, int ino)
{
    /* 1) alloc mem space */
    struct fs001_inode *inode = malloc(sizeof(struct fs001_inode));
    if (inode == NULL)
        goto error_malloc;
    /* 2) read from disk */
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
            struct fs001_dentry_d *dentry_d = inode->data + i;
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
int fs001_write_inode(struct fs001_inode *inode)
{
    /* 1) write inode itself */
    if (driver_write(fs001_super.driver, &inode->d, FS001_INODE_OFF(inode->d.ino), sizeof(struct fs001_inode_d)) != 0)
        goto error_write;
    /* 2) write data (dir, file, ...) */
    if (inode->d.type == FS001_DIR)
    {
        if (inode->d.size == 0)
            return 0;
        if (inode->data != NULL)
            free(inode->data);
        if (inode->d.size != 0 && (inode->data = malloc(inode->d.size)) == NULL)
            goto error_malloc;
        struct fs001_dentry *dentry_cursor = inode->dentries;
        int offset = 0;
        while (dentry_cursor != NULL)
        {
            memcpy(inode->data + offset, &dentry_cursor->d, sizeof(struct fs001_dentry_d));
            offset += sizeof(struct fs001_dentry_d);
            dentry_cursor = dentry_cursor->next;

            if (dentry_cursor->inode != NULL)
                fs001_write_inode(dentry_cursor->inode);
        }
        fs001_write_data(inode);
    }
    else if (inode->d.type == FS001_FILE)
    {
        if (fs001_write_data(inode) != 0)
            goto error_write_data;
    }
    return 0;

error_malloc:
error_write_data:
error_write:
    return -EIO;
}

struct fs001_dentry *fs001_lookup(const char *path, bool *is_find, bool *is_root)
{
    struct fs001_dentry *dentry_cursor = fs001_super.root_dentry;
    struct fs001_dentry *dentry_ret = NULL;
    struct fs001_inode *inode;
    int total_lvl = fs001_calc_lvl(path);
    int lvl = 0;
    bool is_hit;
    char *fname = NULL;
    char *path_cpy = strdup(path);
    *is_root = false;

    if (total_lvl == 0)
    { /* 根目录 */
        *is_find = true;
        *is_root = true;
        dentry_ret = fs001_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");
    while (fname != NULL)
    {
        lvl++;
        if (dentry_cursor->inode == NULL) // lazy load
            if (fs001_read_inode(dentry_cursor, dentry_cursor->d.ino) == NULL)
                goto error_read_inode;

        inode = dentry_cursor->inode;

        if (FS001_IS_REG(inode) && lvl < total_lvl)
        {
            dentry_ret = inode->dentry;
            break;
        }
        if (FS001_IS_DIR(inode))
        {
            dentry_cursor = inode->dentries;
            is_hit = false;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->d.name, fname, strlen(fname)) == 0)
                {
                    is_hit = true;
                    break;
                }
                dentry_cursor = dentry_cursor->next;
            }

            if (!is_hit)
            {
                *is_find = false;
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl)
            {
                *is_find = true;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/");
    }

    if (dentry_ret->inode == NULL)
    {
        dentry_ret->inode = fs001_read_inode(dentry_ret, dentry_ret->d.ino);
    }

    free(path_cpy);
    return dentry_ret;

error_read_inode:
    return NULL;
}
/******************************************************************************
 * SECTION: 必做函数实现
 *******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 *
 * @param conn_info 可忽略，一些建立连接相关的信息
 * @return void*
 */
void *fs001_init(struct fuse_conn_info *conn_info)
{
    /* TODO: 在这里进行挂载 */
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
    if (driver_read(fs001_super.driver, (void *)&fs001_super.d, FS001_SUPER_OFF, sizeof(struct fs001_super_d)) != 0)
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

/**
 * @brief 卸载（umount）文件系统
 *
 * @param p 可忽略
 * @return void
 */
void fs001_destroy(void *p)
{
    /* TODO: 在这里进行卸载 */
    /* 1) forbid reentrance */
    if (fs001_super.driver == NULL)
        return;
    /* 2) write inodes */
    if (fs001_write_inode(fs001_super.root_dentry->inode) != 0)
        perror("fs001_destroy: write inode");
    /* 3) write super block */
    if (driver_write(fs001_super.driver, &fs001_super.d, FS001_SUPER_OFF, sizeof(struct fs001_super_d)) != 0)
        perror("fs001_destroy: write super");
    /* 4) write bitmap */
    if ((driver_write(fs001_super.driver, &fs001_super.bitmap_inode, FS001_BITMAP_INODE_OFF, sizeof(fs001_super.bitmap_inode))) != 0)
        perror("fs001_destroy: write bitmap inode");
    if ((driver_write(fs001_super.driver, &fs001_super.bitmap_data, FS001_BITMAP_DATA_OFF, sizeof(fs001_super.bitmap_data))) != 0)
        perror("fs001_destroy: write bitmap data");
    /* 5) release driver */
    driver_close(fs001_super.driver);
    fs001_super.driver = NULL;

    return;
}

/**
 * @brief 创建目录
 *
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则失败
 */
int fs001_mkdir(const char *path, mode_t mode)
{
    bool is_find, is_root;
    char *fname;
    struct fs001_dentry *last_dentry = fs001_lookup(path, &is_find, &is_root);
    struct fs001_dentry *dentry;
    struct fs001_inode *inode;

    if (is_find)
    {
        return -EEXIST;
    }

    if (FS001_IS_REG(last_dentry->inode))
    {
        return -ENXIO;
    }

    fname = fs001_get_fname(path);
    dentry = fs001_new_dentry(fname, FS001_DIR);
    dentry->parent = last_dentry;
    inode = fs001_alloc_inode(dentry);
    fs001_append_dentry(last_dentry->inode, dentry);

    return 0;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 *
 * @param path 相对于挂载点的路径
 * @param fs001_stat 返回状态
 * @return int 0成功，否则失败
 */
int fs001_getattr(const char *path, struct stat *fs001_stat)
{
    bool is_find, is_root;
    struct fs001_dentry *dentry = fs001_lookup(path, &is_find, &is_root);
    if (is_find == false)
    {
        return -ENOENT;
    }

    if (FS001_IS_DIR(dentry->inode))
    {
        fs001_stat->st_mode = S_IFDIR | FS001_DEFAULT_PERM;
        fs001_stat->st_size = dentry->inode->d.size;
    }
    else if (FS001_IS_REG(dentry->inode))
    {
        fs001_stat->st_mode = S_IFREG | FS001_DEFAULT_PERM;
        fs001_stat->st_size = dentry->inode->d.size;
    }

    fs001_stat->st_nlink = 1;
    fs001_stat->st_uid = getuid();
    fs001_stat->st_gid = getgid();
    fs001_stat->st_atime = time(NULL);
    fs001_stat->st_mtime = time(NULL);
    fs001_stat->st_blksize = FS001_SZ_BLK;

    if (is_root)
    {
        fs001_stat->st_size = 0;
        fs001_stat->st_blocks = fs001_super.blk_disk;
        fs001_stat->st_nlink = 2; /* !特殊，根目录link数为2 */
    }
    return 0;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 *
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 *
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 *
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则失败
 */
int fs001_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                  struct fuse_file_info *fi)
{
    bool is_find, is_root;
    int cur_dir = offset;

    struct fs001_dentry *dentry = fs001_lookup(path, &is_find, &is_root);
    struct fs001_dentry *sub_dentry;
    struct fs001_inode *inode;
    if (is_find)
    {
        inode = dentry->inode;
        sub_dentry = fs001_get_dentry(inode, cur_dir);
        if (sub_dentry)
        {
            filler(buf, sub_dentry->d.name, NULL, ++offset);
        }
        return 0;
    }
    return -ENOENT;
}

/**
 * @brief 创建文件
 *
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则失败
 */
int fs001_mknod(const char *path, mode_t mode, dev_t dev)
{
    bool is_find, is_root;

    struct fs001_dentry *last_dentry = fs001_lookup(path, &is_find, &is_root);
    struct fs001_dentry *dentry;
    struct fs001_inode *inode;
    char *fname;

    if (is_find == true)
    {
        return -EEXIST;
    }

    fname = fs001_get_fname(path);

    if (S_ISREG(mode))
    {
        dentry = fs001_new_dentry(fname, FS001_FILE);
    }
    else if (S_ISDIR(mode))
    {
        dentry = fs001_new_dentry(fname, FS001_DIR);
    }
    else
    {
        dentry = fs001_new_dentry(fname, FS001_FILE);
    }
    dentry->parent = last_dentry;
    inode = fs001_alloc_inode(dentry);
    fs001_append_dentry(last_dentry->inode, dentry);

    return 0;
}

/**
 * @brief 修改时间，为了不让touch报错
 *
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则失败
 */
int fs001_utimens(const char *path, const struct timespec tv[2])
{
    (void)path;
    return 0;
}
/******************************************************************************
 * SECTION: 选做函数实现
 *******************************************************************************/
/**
 * @brief 写入文件
 *
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int fs001_write(const char *path, const char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi)
{
    /* 选做 */
    return size;
}

/**
 * @brief 读取文件
 *
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int fs001_read(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi)
{
    /* 选做 */
    return size;
}

/**
 * @brief 删除文件
 *
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int fs001_unlink(const char *path)
{
    /* 选做 */
    return 0;
}

/**
 * @brief 删除目录
 *
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 *
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int fs001_rmdir(const char *path)
{
    /* 选做 */
    return fs001_unlink(path);
}

/**
 * @brief 重命名文件
 *
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则失败
 */
int fs001_rename(const char *from, const char *to)
{
    /* 选做 */
    int ret = 0;
    bool is_find, is_root;
    struct fs001_dentry *from_dentry = fs001_lookup(from, &is_find, &is_root);
    struct fs001_inode *from_inode;
    struct fs001_dentry *to_dentry;
    mode_t mode = 0;
    if (is_find == false)
    {
        return -ENOENT;
    }

    if (strcmp(from, to) == 0)
    {
        return 0;
    }

    from_inode = from_dentry->inode;

    if (FS001_IS_DIR(from_inode))
    {
        mode = S_IFDIR;
    }
    else if (FS001_IS_REG(from_inode))
    {
        mode = S_IFREG;
    }

    ret = fs001_mknod(to, mode, NULL);
    if (ret != 0)
    { /* 保证目的文件不存在 */
        return ret;
    }

    to_dentry = fs001_lookup(to, &is_find, &is_root);
    fs001_free_inode(to_dentry->inode);   /* 保证生成的inode被释放 */
    to_dentry->d.ino = from_inode->d.ino; /* 指向新的inode */
    to_dentry->inode = from_inode;

    fs001_drop_dentry(from_dentry->parent->inode, from_dentry);
    return ret;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 *
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int fs001_open(const char *path, struct fuse_file_info *fi)
{
    /* 选做 */
    return 0;
}

/**
 * @brief 打开目录文件
 *
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int fs001_opendir(const char *path, struct fuse_file_info *fi)
{
    /* 选做 */
    return 0;
}

/**
 * @brief 改变文件大小
 *
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则失败
 */
int fs001_truncate(const char *path, off_t offset)
{
    /* 选做 */
    bool is_find, is_root;
    struct fs001_dentry *dentry = fs001_lookup(path, &is_find, &is_root);
    struct fs001_inode *inode;

    if (is_find == false)
    {
        return -ENOENT;
    }

    inode = dentry->inode;

    if (FS001_IS_DIR(inode))
    {
        return -EISDIR;
    }

    inode->d.size = offset;

    return 0;
    return 0;
}

/**
 * @brief 访问文件，因为读写文件时需要查看权限
 *
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission.
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence.
 *
 * @return int 0成功，否则失败
 */
int fs001_access(const char *path, int type)
{
    /* 选做: 解析路径，判断是否存在 */
    bool is_find, is_root;
    bool is_access_ok = false;
    struct fs001_dentry *dentry = fs001_lookup(path, &is_find, &is_root);
    struct fs001_inode *inode;

    switch (type)
    {
    case R_OK:
        is_access_ok = true;
        break;
    case F_OK:
        if (is_find)
        {
            is_access_ok = true;
        }
        break;
    case W_OK:
        is_access_ok = true;
        break;
    case X_OK:
        is_access_ok = true;
        break;
    default:
        break;
    }
    return is_access_ok ? 0 : -EACCES;
}
