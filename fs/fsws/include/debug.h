static inline dump() {
    printf("fs001_super.bitmap_inode:\n");
    for (int i = 0; i < 1024; i++)
        printf("%x", fs001_super.bitmap_inode[i]);
    printf("\n");
}