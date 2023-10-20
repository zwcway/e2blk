#ifndef E2BLK_H
#define E2BLK_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <ctype.h>
#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>
#include <et/com_err.h>
#include <sysexits.h>
#include <pthread.h>
#include <math.h>

#include <curses.h>

#define EX_DEVICE 2
#define EX_MEMORY ENOMEM
#define EX_QUIT 256

#define FLAGS_DEBUG 0x02

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#ifdef CURSES
extern void show_error(const char *cmd, int code, const char *fmt, ...);
#define serr show_error
#else
#define serr com_err
#endif
enum {
    CP_BG = 0,
    CP_RED,
    CP_BTN_NL,
    CP_BTN_HL,
    CP_RSV,
    CP_EMP,
    CP_DAT,
    CP_HL,
};



extern const char *prog_name;
extern ext2_filsys fs;
extern unsigned int block_size;
extern unsigned long long device_size;
extern char *device_name;

extern int unicode;

unsigned long long parse_unsigned(const char *str, int size, const char *cmd, const char *descr, int *err);
int win_clear(WINDOW *win, int y, int x, int length);
int readline(const char *promt, char *line, int len);

#endif // E2BLK_H
