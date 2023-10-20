
#include "e2blk.h"

extern int init_ncurses();

const char *prog_name = "e2blk";
ext2_filsys fs;
unsigned int block_size;
unsigned long long device_size;
char *device_name;

static int open_filesystem(int open_flags, blk64_t superblock, blk64_t blocksize) {
    int retval;
    io_manager io_ptr = unix_io_manager;

    if (superblock != 0 && blocksize == 0) {
        com_err(device_name, 0, "if you specify the superblock, you must also specify the block size");
        fs = NULL;
        return EX_USAGE;
    }

    retval = ext2fs_open(device_name, open_flags, superblock, blocksize, io_ptr, &fs);
    if (retval) {
        com_err(prog_name, retval, "while trying to open %s", device_name);
        fs = NULL;
        return EX_DEVICE;
    }
    fs->default_bitmap_type = EXT2FS_BMAP64_RBTREE;

    printf("Reading inode and block bitmaps ... ");

    retval = ext2fs_read_bitmaps(fs);
    if (retval) {
        printf("\n");
        com_err(device_name, retval, "while reading allocation bitmaps");
        goto errout;
    }
    printf("complete\n");

    return 0;

errout:
    retval = ext2fs_close_free(&fs);
    if (retval) {
        com_err(device_name, retval, "while trying to close filesystem");
        return EX_DEVICE;
    }
    return 0;
}

static void close_filesystem() {
    int retval, err;

    if (fs->flags & EXT2_FLAG_IB_DIRTY) {
        retval = ext2fs_write_inode_bitmap(fs);
        if (retval) {
            err++;
            com_err("ext2fs_write_inode_bitmap", retval, 0);
        }
    }
    if (fs->flags & EXT2_FLAG_BB_DIRTY) {
        retval = ext2fs_write_block_bitmap(fs);
        if (retval) {
            err++;
            com_err("ext2fs_write_block_bitmap", retval, 0);
        }
    }
    retval = ext2fs_close_free(&fs);
    if (retval) {
        err++;
        com_err("ext2fs_close", retval, 0);
    }
    if (err)
        exit(EX_DEVICE);

    return;
}

unsigned long long parse_unsigned(const char *str, int size, const char *cmd, const char *descr, int *err) {
    char *tmp;
    unsigned long long ret;

    switch (size) {
    case 4:
        ret = (unsigned long long)strtoul(str, &tmp, 0);
        break;
    case 8:
        ret = strtoull(str, &tmp, 0);
        break;
    case -1:
        ret = (unsigned long long)strtoul(str, &tmp, 0);
        if ((int)ret < 0 || tmp == str)
            goto _error;

        switch (*tmp) {
        case 'G': ret *= -(int)(1 << 30); break;
        case 'g': ret *= -1000000000; break;
        case 'M': ret *= -(int)(1 << 20); break;
        case 'm': ret *= -1000000; break;
        case 'K': ret *= -(int)(1 << 10); break;
        case 'k': ret *= -1000; break;
        case 'B': break;
        case 0: ret = -ret; goto _return;
        default: goto _error;
        }
        tmp++;
    }
_return:
    if (*tmp == 0) {
        if (err)
            *err = 0;
        return ret;
    }
_error:
    serr(cmd, 0, "%s %s", descr, str);
    if (err)
        *err = 1;
    else
        exit(EX_USAGE);
    return 0;
}

static int need_check(int force) {
    if (force)
        return 0;
    int checkit = 0;

    if (fs->super->s_state & EXT2_ERROR_FS)
        checkit = 1;

    if ((fs->super->s_state & EXT2_VALID_FS) == 0)
        checkit = 1;

    if ((ext2fs_free_blocks_count(fs->super) > ext2fs_blocks_count(fs->super)))
        checkit = 1;

    if (fs->super->s_free_inodes_count > fs->super->s_inodes_count)
        checkit = 1;

    return checkit;
}

int main(int argc, char **argv) {
    const char *usage = "Usage: %s [-b blocksize] [-s superblock] [-D] [-V] device\n";
    int c;
    const char *opt_string = "iDVfb:s:";
    int open_flags = EXT2_FLAG_SOFTSUPP_FEATURES | EXT2_FLAG_64BITS | EXT2_FLAG_THREADS | EXT2_FLAG_RW;
    blk64_t superblock = 0;
    int offset_size = 0;
    int force = 0;
    errcode_t ret;

    while ((c = getopt(argc, argv, opt_string)) != EOF) {
        switch (c) {
        case 'i':
            open_flags |= EXT2_FLAG_IMAGE_FILE;
            break;
        case 'D':
            open_flags |= EXT2_FLAG_DIRECT_IO;
            break;
        case 'f':
            force = 1;
            break;
        case 'b':
            block_size = parse_unsigned(optarg, 4, argv[0], "Invalid block size:", NULL);
            break;
        case 's':
            superblock = parse_unsigned(optarg, 8, argv[0], "Invalid superblock block number:", NULL);
            break;
        case 'V':
            /* Print version number and exit */
            fprintf(stderr, "\tUsing %s\n", error_message(EXT2_ET_BASE));
            exit(EX_OK);
        default:
            com_err(argv[0], 0, usage, prog_name);
            return 1;
        }
    }

    if (optind == argc) {
        fprintf(stderr, "Please specify the file system to be opened.\n");
        com_err(argv[0], 0, usage, prog_name);
        exit(EX_USAGE);
    }
    device_name = argv[optind];

    if (ret = open_filesystem(open_flags, superblock, block_size)) {
        exit(ret);
    }
    block_size = EXT2_BLOCK_SIZE(fs->super);

    if (need_check(force)) {
        fprintf(stderr, "Please run 'e2fsck -f %s' first.\n\n", device_name);
        goto _close;
    }

    if (ret = init_ncurses())
        goto _close;
    
_close:

    if (fs)
        close_filesystem();

    exit(ret);
}
