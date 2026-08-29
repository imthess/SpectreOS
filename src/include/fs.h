#ifndef SPECTREOS_FS_H
#define SPECTREOS_FS_H

#include <stdint.h>

#define FS_MAGIC              0x53504653UL
#define FS_VERSION            2

#define FS_MAX_FILES          128
#define FS_MAX_FILENAME       48
#define FS_DIRECT_BLOCKS      32
#define FS_MAX_FILE_SIZE      (FS_DIRECT_BLOCKS * 512)
#define FS_INODE_SIZE         256
#define FS_INODE_SIZE         256

#define FS_SUPERBLOCK_SECTOR  0
#define FS_BITMAP_START       1
#define FS_BITMAP_SECTORS      32
#define FS_INODE_START        33
#define FS_INODE_SECTORS      64
#define FS_INODE_SIZE        256
#define FS_DATA_START          97

#define FS_MODE_FREE          0
#define FS_MODE_FILE          1

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t sector_size;
    uint32_t total_sectors;

    uint32_t bitmap_start;
    uint32_t bitmap_sectors;

    uint32_t inode_start;
    uint32_t inode_sectors;

    uint32_t data_start;
    uint32_t max_files;

    uint32_t root_inode;
    uint32_t reserved[8];

} fs_superblock_t;

typedef struct
{
    uint32_t mode;
    uint32_t size;
    uint32_t blocks;
    uint32_t direct[FS_DIRECT_BLOCKS];
    uint32_t reserved[8];

} fs_inode_t;
#define FS_INODE_SIZE 256

typedef struct
{
    char name[FS_MAX_FILENAME];
    uint32_t inode;
    uint32_t used;
} fs_dirent_t;

typedef struct
{
    int32_t inode;
    uint32_t position;
} fs_handle_t;

int fs_init(void);
int fs_format(void);
int fs_ready(void);

int fs_create(
    const char* name
);

int fs_delete(
    const char* name
);

int fs_open(
    const char* name
);

int fs_close(
    int fd
);

int fs_read(
    int fd,
    void* buffer,
    uint32_t size
);

int fs_write(
    int fd,
    const void* buffer,
    uint32_t size
);

int fs_list(
    char* buffer,
    uint32_t size
);

int fs_exists(
    const char* name
);

#endif
