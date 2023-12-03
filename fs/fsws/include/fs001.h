#ifndef _FS001_H_
#define _FS001_H_

#define FUSE_USE_VERSION 26
#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include "fcntl.h"
#include "string.h"
#include "fuse.h"
#include <stddef.h>
#include "ddriver.h"
#include "errno.h"
#include "types.h"

#define FS001_MAGIC                  /* TODO: Define by yourself */
#define FS001_DEFAULT_PERM    0777   /* 全权限打开 */

/******************************************************************************
* SECTION: fs001.c
*******************************************************************************/
void* 			   fs001_init(struct fuse_conn_info *);
void  			   fs001_destroy(void *);
int   			   fs001_mkdir(const char *, mode_t);
int   			   fs001_getattr(const char *, struct stat *);
int   			   fs001_readdir(const char *, void *, fuse_fill_dir_t, off_t,
						                struct fuse_file_info *);
int   			   fs001_mknod(const char *, mode_t, dev_t);
int   			   fs001_write(const char *, const char *, size_t, off_t,
					                  struct fuse_file_info *);
int   			   fs001_read(const char *, char *, size_t, off_t,
					                 struct fuse_file_info *);
int   			   fs001_access(const char *, int);
int   			   fs001_unlink(const char *);
int   			   fs001_rmdir(const char *);
int   			   fs001_rename(const char *, const char *);
int   			   fs001_utimens(const char *, const struct timespec tv[2]);
int   			   fs001_truncate(const char *, off_t);
			
int   			   fs001_open(const char *, struct fuse_file_info *);
int   			   fs001_opendir(const char *, struct fuse_file_info *);

#endif  /* _fs001_H_ */