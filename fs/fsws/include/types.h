#ifndef _TYPES_H_
#define _TYPES_H_

#define MAX_NAME_LEN    128     

struct custom_options {
	const char*        device;
};

struct fs001_super {
    uint32_t magic;
    int      fd;
    /* TODO: Define yourself */
};

struct fs001_inode {
    uint32_t ino;
    /* TODO: Define yourself */
};

struct fs001_dentry {
    char     name[MAX_NAME_LEN];
    uint32_t ino;
    /* TODO: Define yourself */
};

#endif /* _TYPES_H_ */