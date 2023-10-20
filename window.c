
#include <locale.h>
#include <langinfo.h>
#include "e2blk.h"

extern int do_preview(WINDOW *win);
extern int do_move(WINDOW *win);
static int do_quit(WINDOW *win);

struct button *cur_btn = NULL;
WINDOW *body_win = NULL;
int unicode = 0;
const char *exit_hint = "Press `ESC` or `q` to exit";

struct button {
    int x;
    int y;
    int w;
    const char *name;
    int (*func)(WINDOW *win);
    const char key;
    char selected;
};
struct button button_list[] = {
    {4, -2, 0, "Preview", do_preview, 'p', 0},
    {20, -2, 0, "Move", do_move, 'm', 0},
    {40, -2, 0, "Exit", do_quit, 'q', 0},
    {0},
};

static int do_quit(WINDOW *win) {
    return EX_QUIT;
}

int win_clear(WINDOW *win, int y, int x, int length) {
    int err;
    if (err = wmove(win, y, x))
        return err;

    while (length--)
        if (err = waddch(win, ' '))
            return err;

    return 0;
}

static void draw_button(struct button *btn) {
    int color;
    attron(COLOR_PAIR(CP_BG));
    mvaddch(btn->y, btn->x - 2, btn->key);
    mvaddch(btn->y, btn->x - 1, ':');
    attroff(COLOR_PAIR(CP_BG));
    if (btn->selected)
        color = COLOR_PAIR(CP_BTN_HL);
    else
        color = COLOR_PAIR(CP_BTN_NL);
    attron(color);
    mvprintw(btn->y, btn->x, btn->name);
    attroff(color);
}

static void init_buttons(int x, int y) {
    struct button *btn;
    int color;
    for (btn = &button_list[0]; btn->name; btn++) {
        if (btn->x < 0)
            btn->x = x + btn->x;
        if (btn->y < 0)
            btn->y = y + btn->y;
        btn->w = strlen(btn->name);

        draw_button(btn);
    }

    refresh();
}

static void render_default(int clear) {
    int x, y;

    clear();
    getmaxyx(stdscr, y, x);
    box(stdscr, 0, 0);
    mvwhline(stdscr, y - 1, 1, ACS_HLINE, x - 2);
    attron(COLOR_PAIR(CP_HL));
    mvprintw(y - 1, 2, exit_hint);
    attroff(COLOR_PAIR(CP_HL));

    init_buttons(x, y);

    x = (COLS - 2) / 2 - 25;
    y = (LINES - 4) / 2 - 3;

    if (clear) {
        win_clear(stdscr, y + 0, 1, x - 2);
        win_clear(stdscr, y + 1, 1, x - 2);
        win_clear(stdscr, y + 2, 1, x - 2);
        win_clear(stdscr, y + 3, 1, x - 2);
        win_clear(stdscr, y + 4, 1, x - 2);
    } else {
        mvprintw(y + 0, x + 00, "Device name: %s", fs->device_name);
        mvprintw(y + 1, x + 00, "Mount count: %u", fs->super->s_mnt_count);
        mvprintw(y + 2, x + 00, "Block  size: %u", block_size);
        mvprintw(y + 3, x + 00, "Block count: %llu", ext2fs_blocks_count(fs->super));
        mvprintw(y + 3, x + 25, "Free blocks: %llu", ext2fs_free_blocks_count(fs->super));
        mvprintw(y + 4, x + 00, "Inode count: %u", fs->super->s_inodes_count);
        mvprintw(y + 4, x + 25, "Free inodes: %u", fs->super->s_free_inodes_count);
    }
    move(y, x);
    refresh();
}

static int handle_buttons(char key) {
    struct button *btn;
    int ret;
    for (btn = &button_list[0]; btn->name; btn++) {
        if (btn->key != key)
            continue;
        if (!btn->func)
            continue;

        wclear(body_win);
        ret = btn->func(body_win);
        render_default(0);
        return ret;
    }
}

static int mvprint_multline(WINDOW *win, const char *str, int top, int left) {
    int i, width = 0, linecnt = 0, maxwidth = 0;
    char *c;
    int x, y;
    getmaxyx(win, y, x);

    for (i = strlen(str) - 1; i >= 0; --i) {
        width++;
        if (str[i] == '\n') {
            width = 0;
            linecnt++;
        }
        maxwidth = width > maxwidth ? width : maxwidth;
    }

    if (left <= maxwidth / 2) {
        maxwidth = left * 2 - 2;
        top -= 1;
    }

    top -= linecnt / 2;
    left -= maxwidth / 2;
    if (top < 1)
        top = 1;

    for (c = (char *)str, i = 0, linecnt = 0, width = 0;; i++) {
        width++;
        if (str[i] == '\n' || str[i] == 0 || width >= maxwidth) {
            mvwaddnstr(win, top + linecnt, left, c, width - 1);
            c = (char *)str + i + 1;
            linecnt++;
            width = 0;
        }
        if (str[i] == 0) {
            break;
        }
    }
}

int readline(const char *promt, char *line, int len) {
    int retval;
    int i, x, y, cursor, ret = 0;
    char *c = NULL;

    getmaxyx(stdscr, y, x);

    printf("\033[?1003l\n");
    render_default(1);

    cursor = curs_set(1);
    mvprint_multline(stdscr, promt, y / 2, x / 2);
    win_clear(stdscr, y - 1, 2, strlen(exit_hint));
    attron(COLOR_PAIR(CP_RED));
    mvprintw(y - 1, 2, "Please input: ");
    attroff(COLOR_PAIR(CP_RED));
    refresh();
    noecho();

    for (i = 0;;) {
        int ch = getch();
        switch (ch) {
        case 27:
            ret = EX_QUIT;
        case ERR:
        case '\n': goto _quit;
        case KEY_BACKSPACE:
            if (i < 1)
                break;
            i--;
            x = getcurx(stdscr) - 1;
            x = x < 16 ? 16 : x;
            mvaddch(y - 1, x, ' ');
            move(y - 1, x);
            refresh();
            break;
        default:
            if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                if (i < len) {
                    addch(ch);
                    line[i++] = ch;
                }
            }
        }
    }

_quit:
    line[i] = 0;
    clear();
    printf("\033[?1003h\n");
    render_default(0);
    curs_set(cursor);

    return ret;
}

static int mouse_event() {
    MEVENT event;
    struct button *btn, *last = cur_btn;
    if (getmouse(&event) != OK)
        return 0;

    for (btn = &button_list[0]; btn->name; btn++) {
        btn->selected = btn->x <= event.x && btn->x + btn->w >= event.x && btn->y == event.y;
        if (btn->selected)
            cur_btn = btn;
        else
            cur_btn = NULL;
        draw_button(btn);
    }
    if (last != cur_btn)
        refresh();

    if (event.bstate & BUTTON1_CLICKED) {
        if (cur_btn)
            return handle_buttons(cur_btn->key);
    }

    return 0;
}

static void detect_unicode() {
    const char *lang = getenv("LANG");
    if (lang != NULL && strstr(lang, "UTF-8") != NULL)
        unicode = 1;
    setlocale(LC_CTYPE, "");
    setlocale(LC_ALL, "");
    if (strcmp(nl_langinfo(CODESET), "UTF-8") == 0)
        unicode = 1;
}

void show_error(const char *cmd, int code, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char str[1024] = {0};
    int x, y, cursor;
    WINDOW *win;

    x = strlen(cmd) + 2;
    sprintf(str, "%s: ", cmd);
    vsnprintf(str + x, 1024 - x, fmt, args);
    va_end(args);

    render_default(1);

    getmaxyx(stdscr, y, x);
    win = newwin(y / 2, x / 2, y / 4, x / 4);
    cursor = curs_set(0);
    getmaxyx(win, y, x);
    wbkgd(win, COLOR_PAIR(CP_RED));
    box(win, 0, 0);
    wattron(win, COLOR_PAIR(CP_RED));
    mvprint_multline(win, str, y / 2, x / 2);
    wattroff(win, COLOR_PAIR(CP_RED));

    wrefresh(win);
    keypad(win, TRUE);
    for (;;) {
        int ch = wgetch(win);
        switch (ch) {
        case '\n':
        case 'q': goto _quit;
        }
    }
_quit:
    curs_set(cursor);
}

int init_ncurses() {
    int c, ret;

    detect_unicode();

    initscr();
    cbreak();
    if (LINES < 10 || COLS < 80) {
        endwin();
        com_err(prog_name, 0, "terminal size too small");
        exit(1);
    }

    body_win = newwin(LINES - 3, COLS - 2, 1, 1);
    keypad(stdscr, TRUE);
    /* Don't mask any mouse events */
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    noecho();
    /* Makes the terminal report mouse movement events */
    printf("\033[?1003h\n");

    start_color();
    init_pair(CP_BG, COLOR_WHITE, COLOR_BLACK);
    init_pair(CP_RED, COLOR_RED, COLOR_BLACK);
    init_pair(CP_BTN_NL, COLOR_GREEN, COLOR_BLACK);
    init_pair(CP_BTN_HL, COLOR_GREEN, COLOR_BLUE);
    init_pair(CP_RSV, COLOR_RED, COLOR_WHITE);
    init_pair(CP_EMP, COLOR_WHITE, COLOR_WHITE);
    init_pair(CP_DAT, COLOR_BLUE, COLOR_WHITE);
    init_pair(CP_HL, COLOR_YELLOW, COLOR_GREEN);
    // bkgd((chtype)COLOR_PAIR(CP_BG));

    render_default(0);

    move(0, 0);

    for (;;) {
        int c = wgetch(stdscr);

        switch (c) {
        case 27:
        case 'q': goto _quit;
        case KEY_LEFT:; break;
        case KEY_RIGHT:; break;
        case KEY_MOUSE: ret = mouse_event(); break;
        default: ret = handle_buttons(c); break;
        }

        if (ret == EX_QUIT)
            goto _quit;
    }

_quit:
    /* Disable mouse movement events, as l = low */
    printf("\033[?1003l\n");
    clear();
    endwin();

    if (ret == EX_QUIT)
        return 0;

    return ret;
}
