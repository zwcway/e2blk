#include "e2blk.h"

#define DETAIL_WIN_HEIGHT 2
#define RESET_CURSOR(ctx) (wmove((ctx)->win, (ctx)->y, (ctx)->x))
#define FISSET(a, b) ((a) & (b))
#define FSET(a, b) ((a) |= (b))
#define FUNSET(a, b) ((a) &= ~(b))

#define FLAG_PRINTED 0x01
#define FLAG_SELECTED 0x02

struct print_block_cell {
    __u8 flag;
    __u8 color;  //
    __u16 pos;   // cell index
    __u32 count; // blocks in cell
    __u32 size;
};

struct print_block_context {
    WINDOW *win;
    int x;      // 总宽度
    int y;      // 总高度
    int left;   // 左边距
    int top;    // 上边距
    int width;  // 表宽
    int height; // 表高
    int count;
    __u64 blocks;
    int pos;

    ext2_ino_t ino;
    __u64 isize;
    struct ext2_inode *inode;
    struct print_block_cell *blocks_start;
    struct print_block_cell *blocks_end;
};
int current_blk = 0;

static void print_blocks(struct print_block_context *ctx, struct print_block_cell *bc) {
    double percent;
    __u8 color;
    char *ch = NULL;
    int x, y;

    if (bc->color == 0)
        return;

    if (FISSET(bc->flag, FLAG_PRINTED))
        return;

#if 1
    if (unicode)
        ch = FISSET(bc->flag, FLAG_SELECTED) ? "\u263A" : ch;
    else
        ch = FISSET(bc->flag, FLAG_SELECTED) ? "*" : ch;
#endif

    if (!ch)
        if (unicode) {
            percent = (double)bc->count / bc->size;

            if (percent >= 1.0)
                ch = "\u2588"; //"\xE2\x96\x88";
            else if (percent > 0.9)
                ch = "\u2587"; //"\xE2\x96\x87";
            else if (percent > 0.8)
                ch = "\u2586"; //"\xE2\x96\x86";
            else if (percent > 0.7)
                ch = "\u2585"; //"\xE2\x96\x85";
            else if (percent > 0.6)
                ch = "\u2584"; //"\xE2\x96\x84";
            else if (percent > 0.5)
                ch = "\u2583"; //"\xE2\x96\x83";
            else if (percent > 0.4)
                ch = "\u2582"; //"\xE2\x96\x82";
            else if (percent > 0.0)
                ch = "\u2581"; //"\xE2\x96\x81";
            else
                ch = "\u2588";
        } else
            ch = "@";

    color = FISSET(bc->flag, FLAG_SELECTED) ? CP_HL : bc->color;
    x = ctx->left + bc->pos % ctx->width;
    y = ctx->top + bc->pos / ctx->width;

    // if (bc == ctx->blocks_start || (bc - 1)->color != bc->color)
    wattron(ctx->win, COLOR_PAIR(color));

    mvwprintw(ctx->win, y, x, (const char *)ch);

    // if (bc == ctx->blocks_end || (bc + 1)->color != bc->color)
    wattroff(ctx->win, COLOR_PAIR(color));

    FSET(bc->flag, FLAG_PRINTED);

    // wmove(ctx->win, y, x);
    wrefresh(ctx->win);
}

static char *format_bytes(__u64 bytes, char *result, size_t len) {
    if (bytes >= 1024ULL * 1024 * 1024 * 1024)
        snprintf(result, len, "%.2fTB", (double)bytes / (1024ULL * 1024 * 1024 * 1024));
    else if (bytes >= 1024 * 1024 * 1024)
        snprintf(result, len, "%.2fGB", (float)bytes / (1024 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        snprintf(result, len, "%.2fMB", (float)bytes / (1024 * 1024));
    else if (bytes >= 1024)
        snprintf(result, len, "%.2fKB", (float)bytes / 1024);
    else
        snprintf(result, len, "%zuB", bytes);

    return result;
}

static int count_digits(int num) {
    int count = 1;

    if (num < 0) {
        num = -num;
        count++;
    }

    while (num) {
        num /= 10;
        count++;
    }

    return count;
}
static int show_detail(struct print_block_context *ctx, int offset, int blk_index) {
    struct print_block_cell *blk;
    int last = current_blk;
    __u64 tmp1, tmp2;
    char size[16];

    if (blk_index >= 0 && blk_index < ctx->count)
        current_blk = blk_index;

    if (offset <= 0 && current_blk < 0) {
        mvwprintw(ctx->win, ctx->height + 0, 00, "Use Arrow key to view details");
        return 0;
    }

    if (current_blk < 0)
        current_blk = 0;
    else if (current_blk + offset >= 0 && current_blk + offset < ctx->count)
        current_blk += offset;

    if (last != current_blk && last >= 0 && last < ctx->count) {
        blk = ctx->blocks_start + last;
        FUNSET(blk->flag, FLAG_PRINTED);
        FUNSET(blk->flag, FLAG_SELECTED);
        print_blocks(ctx, blk);
    }
    if (last == current_blk)
        return 0;

    win_clear(ctx->win, ctx->height + 0, 0, ctx->width);
    win_clear(ctx->win, ctx->height + 1, 0, ctx->width);
    win_clear(ctx->win, ctx->height + 2, 0, ctx->width);

    blk = ctx->blocks_start + current_blk;
    mvwprintw(ctx->win, ctx->height + 0, 00, "Pack #%d (%d)", blk->pos, blk->size);
    tmp1 = (__u64)((double)ctx->blocks / ctx->count * (blk->pos)) * block_size + 1;
    tmp2 = (__u64)((double)ctx->blocks / ctx->count * (blk->pos + 1)) * block_size;
    mvwprintw(ctx->win, ctx->height + 0, 10 + count_digits(blk->pos) + count_digits(blk->size), "Disk Range: %llu-%llu(%s)",
              tmp1,
              tmp2,
              format_bytes(tmp2, size, 15));
    mvwprintw(ctx->win, ctx->height + 1, 5, "Blocks: %d", blk->count);
    mvwprintw(ctx->win, ctx->height + 1, 5 + 10 + count_digits(blk->count), "Size: %s", format_bytes(blk->count * block_size, size, 15));
    tmp1 = (__u64)((double)ctx->blocks / ctx->count * (blk->pos)) + 1;
    tmp2 = (__u64)((double)ctx->blocks / ctx->count * (blk->pos + 1));
    mvwprintw(ctx->win, ctx->height + 1, 5 + 10 + count_digits(blk->count) + 8 + strlen(size), "Range: %llu-%llu",
              tmp1,
              tmp2);
    mvwprintw(ctx->win, ctx->height + 2, 00, "Pack Count: %d", ctx->count);

    FUNSET(blk->flag, FLAG_PRINTED);
    FSET(blk->flag, FLAG_SELECTED);
    print_blocks(ctx, blk);

    // RESET_CURSOR(ctx);
    return 0;
}

static int mouse_event(struct print_block_context *ctx) {
    MEVENT event;
    if (getmouse(&event) != OK)
        return 0;
    if (event.x > ctx->width - ctx->left || event.y > ctx->height - ctx->top)
        return 0;

    // if (event.bstate & BUTTON1_CLICKED)
    show_detail(ctx, 0, (event.x - 1 - ctx->left) + (event.y - 1 - ctx->top) * ctx->width);
}

static void *thread_walk_blocks(void *arg) {
    struct print_block_context *ctx = (struct print_block_context *)arg;
    __u64 blknum;
    struct print_block_cell *bc, *last = NULL;
    int i, size;
    int idx, has, ret = 0;
    double blk_cells, cell_blks;

    blk_cells = (double)ctx->count / ctx->blocks;
    cell_blks = (double)ctx->blocks / ctx->count;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    for (blknum = fs->super->s_first_data_block; blknum < ctx->blocks; blknum++) {
        has = ext2fs_test_block_bitmap2(fs->block_map, blknum);

        idx = (int)(blk_cells * (blknum - fs->super->s_first_data_block + 1));
        size = (int)((__u64)(cell_blks * (idx + 1)) - (__u64)(cell_blks * idx));

        if (idx < 0 || idx >= ctx->count)
            continue;
        bc = ctx->blocks_start + idx;
        // if (bc->size && bc->count >= bc->size)
        //     bc++;

        if (bc->color == 0) { // init it
            bc->pos = idx;
            bc->color = CP_EMP;
            bc->size = size;
        }
        bc->count += !!has;

        if (last != bc) {
            if (last) {
                if (last->count)
                    last->color = CP_DAT;
                else
                    last->color = CP_EMP;
                print_blocks(ctx, last);
            }
            last = bc;
        }
    }
    print_blocks(ctx, bc);
    show_detail(ctx, 0, -1);

_exit:
    pthread_exit(NULL);
}

int do_preview(WINDOW *win) {
    ext2fs_block_bitmap block_bitmap;
    int ret = 0;
    struct print_block_context ctx = {0};
    int size, cursor;
    pthread_t thread;

    // wbkgd(win, COLOR_PAIR(CP_BG));
    cursor = curs_set(0);
    ctx.win = win;
    getmaxyx(win, ctx.y, ctx.x);
    ctx.left = 0;
    ctx.top = 0;
    ctx.width = ctx.x - 2 * ctx.left;
    ctx.height = ctx.y - 2 * ctx.top - DETAIL_WIN_HEIGHT;

    current_blk = -1;
    ctx.count = ctx.width * ctx.height;
    ctx.blocks = ext2fs_blocks_count(fs->super);

    size = sizeof(struct print_block_cell) * (ctx.count + 1);
    ctx.blocks_start = (struct print_block_cell *)malloc(size);
    if (ctx.blocks_start == NULL) {
        serr(prog_name, 0, "no memory", NULL);
        return EX_MEMORY;
    }
    memset(ctx.blocks_start, 0, size);
    ctx.blocks_end = ctx.blocks_start + ctx.count + 1;

    if (pthread_create(&thread, NULL, thread_walk_blocks, &ctx)) {
        serr(prog_name, 0, "create thread error", NULL);
        ret = EX_OSERR;
        goto _exit;
    }
    pthread_detach(thread);

    keypad(win, TRUE);
    for (;;) {
        int c = wgetch(win);
        switch (c) {
        case 27: ret = EX_QUIT; goto _exit;
        case 'q': goto _exit;
        case KEY_LEFT: show_detail(&ctx, -1, -1); break;
        case KEY_RIGHT: show_detail(&ctx, 1, -1); break;
        case KEY_UP: show_detail(&ctx, -ctx.width, -1); break;
        case KEY_DOWN: show_detail(&ctx, +ctx.width, -1); break;
        case KEY_END: show_detail(&ctx, ctx.width - (current_blk % ctx.width) - 1, -1); break;
        case KEY_HOME: show_detail(&ctx, -(current_blk % ctx.width), -1); break;
        case KEY_MOUSE: mouse_event(&ctx); break;
        default:
            break;
        }
    }

_exit:
    pthread_cancel(thread);

    free(ctx.blocks_start);

    curs_set(cursor);
    return ret;
}
