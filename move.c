#include <e2p/e2p.h>
#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>

#include "e2blk.h"

struct inode_search_context {
    e2_blkcnt_t block;
    int ret;
};

static int inode_blocks_proc(ext2_filsys fs EXT2FS_ATTR((unused)),
                             blk64_t *blocknr,
                             e2_blkcnt_t blockcnt EXT2FS_ATTR((unused)),
                             blk64_t ref_block EXT2FS_ATTR((unused)),
                             int ref_offset EXT2FS_ATTR((unused)),
                             void *private) {
    struct inode_search_context *ctx = (struct inode_search_context *)private;

    if (*blocknr == ctx->block) {
        ctx->ret = 1;
        return BLOCK_ABORT;
    }

    // printf("%d, ", *blocknr);

    return 0;
}

static int find_inode_by_block(e2_blkcnt_t block, struct ext2_inode *found, ext2_ino_t *ino) {
    struct inode_search_context ctx = {0};
    ctx.block = block;

    for (*ino = EXT2_ROOT_INO; *ino < fs->super->s_inodes_count; (*ino)++) {
        if (ext2fs_read_inode(fs, *ino, found))
            continue;

        // printf("%s", found->);
        // 循环inode所有blocks
        ext2fs_block_iterate3(fs, *ino, BLOCK_FLAG_READ_ONLY, NULL, inode_blocks_proc, &ctx);
        if (ctx.ret) {
            return 0;
        }
    }

    return 1;
}

struct process_block_context {
    ext2_ino_t ino;
    struct ext2_inode *inode;
    ext2fs_block_bitmap reserve;
    ext2fs_block_bitmap alloc_map;
    errcode_t error;
    char *buf;
    int add_dir;
    int flags;
};

static int process_and_move_block(ext2_filsys fs,
                                  blk64_t *block_nr,
                                  e2_blkcnt_t blockcnt,
                                  blk64_t ref_block,
                                  int ref_offset,
                                  void *priv_data) {
    struct process_block_context *pb;
    errcode_t retval;
    int ret;
    blk64_t block, orig;

    pb = (struct process_block_context *)priv_data;
    block = orig = *block_nr;
    ret = 0;

    /*
     * Let's see if this is one which we need to relocate
     */
    if (ext2fs_test_block_bitmap2(pb->reserve, block)) {
        // TODO 确保node所有的block连续
        do {
            if (++block >= ext2fs_blocks_count(fs->super))
                block = fs->super->s_first_data_block;

            if (block == orig) {
                retval = EXT2_ET_BLOCK_ALLOC_FAIL;
                goto _exit;
            }
        } while (ext2fs_test_block_bitmap2(pb->reserve, block) || ext2fs_test_block_bitmap2(pb->alloc_map, block));

        if (retval = io_channel_read_blk64(fs->io, orig, 1, pb->buf))
            goto _exit;

        if (retval = io_channel_write_blk64(fs->io, block, 1, pb->buf))
            goto _exit;

        *block_nr = block;
        ext2fs_mark_block_bitmap2(pb->alloc_map, block);
        ret = BLOCK_CHANGED;

        if (pb->flags & FLAGS_DEBUG)
            printf("ino=%u, blockcnt=%lld, %llu->%llu\n",
                   (unsigned)pb->ino, blockcnt,
                   (unsigned long long)orig,
                   (unsigned long long)block);
    }
    if (pb->add_dir) {
        if (retval = ext2fs_add_dir_block2(fs->dblist, pb->ino, block, blockcnt))
            goto _exit;
    }

_exit:
    pb->error = retval;

    return ret | BLOCK_ABORT;
}

static int move_inode(ext2_ino_t ino, struct ext2_inode *inode, __u64 after_block) {
    errcode_t retval;
    struct process_block_context pb;
    char *block_buf;

    pb.reserve = fs->block_map;
    pb.error = 0;
    pb.alloc_map = NULL;
    pb.flags = 0;

    ext2fs_copy_bitmap(fs->block_map, &pb.alloc_map);
    while (after_block > 0)
        ext2fs_mark_block_bitmap2(pb.alloc_map, after_block--);

    if (retval = ext2fs_get_array(4, fs->blocksize, &block_buf))
        goto _done;

    pb.buf = block_buf + fs->blocksize * 3;

    if ((inode->i_links_count == 0) || !ext2fs_inode_has_valid_blocks2(fs, inode))
        goto _done;

    pb.ino = ino;
    pb.inode = inode;

    pb.add_dir = LINUX_S_ISDIR(inode->i_mode) && fs->dblist;

    retval = ext2fs_block_iterate3(fs, ino, 0, block_buf, process_and_move_block, &pb);
    if (retval)
        goto _done;

    if (retval = pb.error)
        goto _done;

_done:
    ext2fs_free_mem(&block_buf);
    ext2fs_free_block_bitmap(pb.alloc_map);
    return retval;
}

static int is_mounted() {
    errcode_t retval;
    int len, mount_flags;
    char *mtpt;
    len = 80;
    while (1) {
        mtpt = malloc(len);
        if (!mtpt)
            return ENOMEM;
        mtpt[len - 1] = 0;
        retval = ext2fs_check_mount_point(device_name, &mount_flags, mtpt, len);
        if (retval) {
            free(mtpt);
            serr("ext2fs_check_mount_point", retval, "while determining whether %s is mounted.", device_name);
            return 1;
        }
        if (mount_flags & EXT2_MF_MOUNTED && 0 == mtpt[len - 1]) {
            serr(device_name, retval, "is mounted on %s .need unmount it.", mtpt);
            free(mtpt);
            return 1;
        } else if (!(mount_flags & EXT2_MF_MOUNTED))
            break;
        free(mtpt);
        len = 2 * len;
    }
    return 0;
}

int do_move(WINDOW *win) {
    __u64 blknum, tmp;
    struct ext2_inode inode;
    ext2_ino_t ino;
    errcode_t retval;
    char input[16];
    int x, y, offset;

    getmaxyx(win, y, x);

    /* filesystem must not mounted*/
    if (retval = is_mounted()) {
        return retval;
    }
    do {
        if (retval = readline("Input the offset size.\n"
                              "size must power two or xxxB(unit blocksize).\n"
                              "support unit in B,K,k,M,m,G,g.\n"
                              "eg. '2M' is 2097152\n"
                              "'2m' is 2000000\n"
                              "'2B' is 2 x block size",
                              input, 15)) {
            if (retval == EX_QUIT)
                return 0;
            return retval;
        }

        offset = (int)parse_unsigned(input, -1, prog_name, "invalid", (int *)&retval);
        if (!retval && offset < 0) {
            offset = -offset;
            if (offset % block_size) {
                serr(prog_name, 0, "offsetsize invalid", NULL);
                retval = EX_USAGE;
            } else {
                offset = offset / block_size;
                if (ext2fs_free_blocks_count(fs->super) * block_size < offset) {
                    serr(device_name, 0, "does not have enough space", NULL);
                    retval = EX_DEVICE;
                }
            }
        }
    } while (retval);

    /* */
    for (blknum = offset; blknum > 0; blknum--) {
        if (blknum / 1000) {
        }
        if (!ext2fs_test_block_bitmap2(fs->block_map, blknum))
            continue;

        if (find_inode_by_block(blknum, &inode, &ino)) {
            serr(prog_name, 0, "can not found inode in block %d"
                               " quit.",
                 blknum);
            return EX_OSERR;
        }
        if (move_inode(ino, &inode, offset)) {
            serr(prog_name, 0, "can not move inode %u in block %llu"
                               " quit.",
                 ino, blknum);
            return EX_OSERR;
        }
    }

    keypad(win, TRUE);
    for (;;) {
        int ch = wgetch(win);
        switch (ch) {
        case 'q': retval = EX_QUIT; goto _quit;
        }
    }

_quit:
    return 0;
}
