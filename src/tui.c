/* gg — terminal user interface: raw mode, ANSI drawing, widgets. */
#include "gg.h"

#include <sys/ioctl.h>
#include <termios.h>

tui_state T;
sbuf G_FRAME;

#define ALT_ON "\033[?1049h"
#define ALT_OFF "\033[?1049l"
#define CUR_HIDE GG_CUR_HIDE
#define CUR_SHOW GG_CUR_SHOW
#define CLR "\033[2J"
#define HOME "\033[H"

static int g_atexit_done;

static const char *b_top_left, *b_top_right, *b_bot_left, *b_bot_right;
static const char *b_h, *b_v, *b_left_tee, *b_right_tee, *b_sel, *b_mark;

static void set_glyphs(void) {
  if (T.utf8) {
    b_top_left = "\u250c";
    b_top_right = "\u2510";
    b_bot_left = "\u2514";
    b_bot_right = "\u2518";
    b_h = "\u2500";
    b_v = "\u2502";
    b_left_tee = "\u251c";
    b_right_tee = "\u2524";
    b_sel = "\u258c";   /* ▌ */
    b_mark = "\u25b8";  /* ▸ */
  } else {
    b_top_left = "+";
    b_top_right = "+";
    b_bot_left = "+";
    b_bot_right = "+";
    b_h = "-";
    b_v = "|";
    b_left_tee = "+";
    b_right_tee = "+";
    b_sel = ">";
    b_mark = ">";
  }
}

static int locale_is_utf8(void) {
  const char *v = getenv("GG_UTF8");
  if (v && *v) return !(gg_streq(v, "0") || gg_streq(v, "no"));
#if GG_WINDOWS
  return 1; /* cosmopolitan configures the console code page to UTF-8 */
#endif
  const char *l = getenv("LC_ALL");
  if (!l || !*l) l = getenv("LC_CTYPE");
  if (!l || !*l) l = getenv("LANG");
  if (!l || !*l) return 0;
  if (strstr(l, "UTF-8") || strstr(l, "utf-8") || strstr(l, "UTF8") ||
      strstr(l, "utf8"))
    return 1;
  return 0;
}

void tui_probe(void) {
  static int done;
  if (done) return;
  done = 1;
  memset(&T, 0, sizeof(T));
  T.tty_out = isatty(1);
  T.tty_in = isatty(0);
  T.utf8 = locale_is_utf8();
  const char *plain = getenv("GG_PLAIN");
  const char *term = getenv("TERM");
  T.interactive = T.tty_out && T.tty_in && !(plain && *plain) &&
                  !(term && gg_streq(term, "dumb")) && getenv("GG_NO_TUI") == 0;
  T.no_color = !T.tty_out;
  sb_init(&G_FRAME, 8192);
  set_glyphs();
  tui_size();
}

void tui_restore(void) {
  if (T.raw || T.in_alt) {
    sbuf b;
    sb_init(&b, 64);
    sb_adds(&b, ST_RESET);
    sb_adds(&b, CUR_SHOW);
    sb_adds(&b, ALT_OFF);
    if (T.raw && T.saved_ok) tcsetattr(0, TCSAFLUSH, &T.saved);
    write(1, b.p, b.n);
    sb_free(&b);
  }
  T.raw = 0;
  T.in_alt = 0;
}

static void on_signal(int sig) {
  tui_restore();
  signal(sig, SIG_DFL);
  raise(sig);
}

int tui_init(void) {
  tui_probe();
  if (g_atexit_done) return T.interactive;
  g_atexit_done = 1;
  atexit(tui_restore);
  if (T.interactive) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGQUIT, on_signal);
  }
  return T.interactive;
}

void tui_size(void) {
  struct winsize ws;
  int w = 0, h = 0;
  if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col) {
    w = ws.ws_col;
    h = ws.ws_row;
  }
  if ((!w || !h) && ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_col) {
    w = ws.ws_col;
    h = ws.ws_row;
  }
  if (!w) w = 80;
  if (!h) h = 24;
  if (w < 20) w = 20;
  if (h < 6) h = 6;
  T.w = w;
  T.h = h;
}

void tui_enter(void) {
  if (!T.interactive || T.in_alt) return;
  tui_size();
  if (!T.saved_ok) {
    if (tcgetattr(0, &T.saved) == 0) T.saved_ok = 1;
  }
  struct termios raw;
  if (tcgetattr(0, &raw) == 0) {
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= (tcflag_t) ~OPOST;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &raw);
    T.raw = 1;
  }
  sbuf b;
  sb_init(&b, 64);
  sb_adds(&b, ALT_ON CLR CUR_HIDE);
  write(1, b.p, b.n);
  sb_free(&b);
  T.in_alt = 1;
}

void tui_leave(void) {
  if (!T.interactive) return;
  if (T.in_alt) {
    sbuf b;
    sb_init(&b, 64);
    sb_adds(&b, ST_RESET CUR_SHOW ALT_OFF);
    write(1, b.p, b.n);
    sb_free(&b);
  }
  if (T.raw && T.saved_ok) tcsetattr(0, TCSAFLUSH, &T.saved);
  T.raw = 0;
  T.in_alt = 0;
}

void tui_flush(sbuf *b) {
  if (b->n) {
    size_t off = 0;
    while (off < b->n) {
      ssize_t rc = write(1, b->p + off, b->n - off);
      if (rc <= 0) break;
      off += (size_t)rc;
    }
  }
  sb_clear(b);
}

void tui_clear(sbuf *b) {
  sb_adds(b, ST_RESET);
  sb_adds(b, HOME);
  sb_adds(b, "\033[2J");
}

void tui_at(sbuf *b, int row, int col) {
  sb_addf(b, "\033[%d;%dH", row, col);
}

void tui_repeat(sbuf *b, const char *s, int times) {
  for (int i = 0; i < times; i++) sb_adds(b, s);
}

void tui_hline(sbuf *b, int row, int col, int len, const char *style) {
  tui_at(b, row, col);
  if (style) sb_adds(b, style);
  for (int i = 0; i < len; i++) sb_adds(b, b_h);
  sb_adds(b, ST_RESET);
}

const char *tui_style(const char *style) {
  if (!T.interactive || T.no_color) return "";
  return style;
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

static unsigned char g_inbuf[4096];
static size_t g_inlen, g_inpos;

/* returns bytes available in the pending buffer */
static size_t in_pending(void) { return g_inlen - g_inpos; }

static int in_fill(int timeout_ms) {
  if (in_pending()) return 1;
  g_inlen = g_inpos = 0;
  struct pollfd p;
  p.fd = 0;
  p.events = POLLIN;
  p.revents = 0;
  int rc = poll(&p, 1, timeout_ms);
  if (rc <= 0) return 0;
  ssize_t n = read(0, g_inbuf, sizeof(g_inbuf));
  if (n <= 0) return 0;
  g_inlen = (size_t)n;
  g_inpos = 0;
  return 1;
}

static int in_byte(void) {
  if (!in_fill(0)) return -1;
  return g_inbuf[g_inpos++];
}

/* parse one key; timeout 0 = non blocking (returns KEY_NONE) */
int tui_key(int timeout_ms) {
  if (!T.tty_in) {
    /* non interactive: a lone read() line, or EOF */
    int c = getchar();
    if (c == EOF) return KEY_NONE;
    if (c == '\n') return KEY_ENTER;
    return c;
  }
  if (!in_fill(timeout_ms)) return KEY_NONE;
  int c = in_byte();
  if (c < 0) return KEY_NONE;
  if (c != 0x1B) {
    if (c >= 0x80) {
      /* utf-8 multibyte: gather continuation bytes */
      char tmp[8];
      int n = 1;
      tmp[0] = (char)c;
      int need = 1;
      unsigned char u = (unsigned char)c;
      if ((u & 0xE0) == 0xC0) need = 2;
      else if ((u & 0xF0) == 0xE0) need = 3;
      else if ((u & 0xF8) == 0xF0) need = 4;
      while (n < need) {
        int nx = in_byte();
        if (nx < 0) {
          if (!in_fill(20)) break;
          nx = in_byte();
          if (nx < 0) break;
        }
        tmp[n++] = (char)nx;
      }
      tmp[n] = 0;
      uint32_t cp;
      gg_utf8_decode(tmp, (size_t)n, &cp);
      return (int)cp;
    }
    return c;
  }
  /* escape sequence */
  int c1 = in_byte();
  if (c1 < 0) {
    if (!in_fill(40)) return KEY_ESC;
    c1 = in_byte();
    if (c1 < 0) return KEY_ESC;
  }
  if (c1 == '[' || c1 == 'O') {
    int c2 = in_byte();
    if (c2 < 0) {
      in_fill(40);
      c2 = in_byte();
      if (c2 < 0) return KEY_ESC;
    }
    if (c1 == 'O') {
      switch (c2) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case 'P': return KEY_F1;
        default: return KEY_NONE;
      }
    }
    if (c2 >= '0' && c2 <= '9') {
      /* CSI <num>(;<num>)?[~A-Za-z] */
      int nums[4] = {0, 0, 0, 0};
      int ni = 0;
      int cur = c2 - '0';
      for (;;) {
        int x = in_byte();
        if (x < 0) {
          in_fill(20);
          x = in_byte();
          if (x < 0) return KEY_NONE;
        }
        if (x >= '0' && x <= '9') {
          cur = cur * 10 + (x - '0');
          continue;
        }
        if (x == ';') {
          if (ni < 3) nums[ni++] = cur;
          cur = 0;
          continue;
        }
        if (ni < 3) nums[ni++] = cur;
        int mod = (ni >= 2) ? nums[1] : 0;
        switch (x) {
          case 'A': return mod == 5 ? KEY_CTRL_UP : KEY_UP;
          case 'B': return mod == 5 ? KEY_CTRL_DOWN : KEY_DOWN;
          case 'C': return mod == 5 ? KEY_CTRL_RIGHT : KEY_RIGHT;
          case 'D': return mod == 5 ? KEY_CTRL_LEFT : KEY_LEFT;
          case 'H': return KEY_HOME;
          case 'F': return KEY_END;
          case '~':
            switch (nums[0]) {
              case 1: return KEY_HOME;
              case 2: return KEY_DEL;
              case 3: return KEY_DEL;
              case 4: return KEY_END;
              case 5: return KEY_PGUP;
              case 6: return KEY_PGDN;
              case 7: return KEY_HOME;
              case 8: return KEY_END;
              case 200: return KEY_HOME;
              default: return KEY_NONE;
            }
          default: return KEY_NONE;
        }
      }
    }
    switch (c2) {
      case 'A': return KEY_UP;
      case 'B': return KEY_DOWN;
      case 'C': return KEY_RIGHT;
      case 'D': return KEY_LEFT;
      case 'H': return KEY_HOME;
      case 'F': return KEY_END;
      case 'Z': return KEY_SHIFT_TAB;
      default: return KEY_NONE;
    }
  }
  return KEY_ESC;
}

int tui_keys_pending(void) { return in_pending() > 0; }

void tui_bell(void) { write(1, "\a", 1); }

/* ------------------------------------------------------------------ */
/* shared frame helpers                                                */
/* ------------------------------------------------------------------ */

static void draw_header(sbuf *b, const char *title, const char *right) {
  tui_at(b, 1, 1);
  sb_adds(b, tui_style(ST_TITLE));
  sb_adds(b, " gg ");
  sb_adds(b, tui_style(ST_MUTED));
  sb_adds(b, b_v);
  sb_adds(b, " ");
  sb_adds(b, tui_style(ST_BOLD));
  sb_adds(b, title ? title : "");
  sb_adds(b, ST_RESET);
  if (right && *right) {
    int rw = gg_width(right);
    int col = T.w - rw - 2;
    if (col > 0) {
      tui_at(b, 1, col);
      sb_adds(b, tui_style(ST_MUTED));
      sb_adds(b, right);
      sb_adds(b, ST_RESET);
    }
  }
  tui_hline(b, 2, 1, T.w, tui_style(ST_DIM));
}

static void draw_footer(sbuf *b, const char *keys) {
  tui_hline(b, T.h - 1, 1, T.w, tui_style(ST_DIM));
  tui_at(b, T.h, 1);
  sb_adds(b, " ");
  sb_adds(b, tui_style(ST_MUTED));
  sb_adds(b, keys ? keys : "");
  sb_adds(b, ST_RESET);
}

/* ------------------------------------------------------------------ */
/* menu                                                               */
/* ------------------------------------------------------------------ */

void tui_menu_init(tui_menu *m, const char *title, const char **items, int n) {
  memset(m, 0, sizeof(*m));
  m->title = title;
  m->items = items;
  m->n = n;
  m->sel = 0;
  m->filter_on = 1;
}

static int menu_matches(tui_menu *m, int i) {
  if (!m->filter[0]) return 1;
  const char *it = m->items[i];
  /* case-insensitive substring match */
  size_t fl = strlen(m->filter);
  for (const char *p = it; *p; p++) {
    size_t k = 0;
    while (k < fl && p[k] &&
           tolower((unsigned char)p[k]) == tolower((unsigned char)m->filter[k]))
      k++;
    if (k == fl) return 1;
  }
  return 0;
}

static int menu_filtered_count(tui_menu *m) {
  int c = 0;
  for (int i = 0; i < m->n; i++)
    if (menu_matches(m, i)) c++;
  return c;
}

static int menu_filtered_index(tui_menu *m, int nth) {
  int c = 0;
  for (int i = 0; i < m->n; i++) {
    if (menu_matches(m, i)) {
      if (c == nth) return i;
      c++;
    }
  }
  return -1;
}

static int menu_nth_of(tui_menu *m, int index) {
  int c = 0;
  for (int i = 0; i < m->n; i++) {
    if (menu_matches(m, i)) {
      if (i == index) return c;
      c++;
    }
  }
  return 0;
}

void tui_menu_render(tui_menu *m) {
  sbuf *b = &G_FRAME;
  tui_size();
  tui_clear(b);
  draw_header(b, m->title, m->status);
  int rows = T.h - 4; /* header(2) + filter + footer */
  if (rows < 1) rows = 1;
  int total = menu_filtered_count(m);
  int nth = menu_nth_of(m, m->sel);
  if (nth >= m->scroll + rows) m->scroll = nth - rows + 1;
  if (nth < m->scroll) m->scroll = nth;
  if (m->scroll < 0) m->scroll = 0;

  for (int r = 0; r < rows; r++) {
    int nth_i = m->scroll + r;
    int idx = menu_filtered_index(m, nth_i);
    int row = 3 + r;
    tui_at(b, row, 1);
    sb_adds(b, "\033[K");
    if (idx < 0) continue;
    const char *it = m->items[idx];
    int tw = T.w - 2;
    if (tw < 4) tw = 4;
    if (nth_i == nth) {
      sb_adds(b, tui_style(ST_SEL));
      sb_adds(b, " ");
      sb_adds(b, b_mark);
      sb_adds(b, " ");
      sb_adds(b, ST_RESET);
      sb_adds(b, tui_style(ST_BOLD));
      sb_adds(b, it);
      sb_adds(b, ST_RESET);
    } else {
      sb_adds(b, "   ");
      sb_adds(b, it);
    }
  }
  /* filter / status line */
  tui_at(b, T.h - 2, 1);
  sb_adds(b, "\033[K");
  if (m->filter_on) {
    sb_adds(b, tui_style(ST_MUTED));
    sb_adds(b, m->filter[0] ? " filter: " : " filter: ");
    sb_adds(b, ST_RESET);
    sb_adds(b, m->filter[0] ? m->filter : tui_style(ST_DIM));
    if (!m->filter[0]) sb_adds(b, gg_tr("type to search", "输入以搜索"));
    sb_adds(b, ST_RESET);
    sb_addf(b, "%s  (%d/%d)%s", tui_style(ST_MUTED), total, m->n, ST_RESET);
  }
  draw_footer(b, m->footer ? m->footer
                           : gg_tr("\xe2\x86\x91\xe2\x86\x93 move  \xe2\x86\xb5 run  "
                                   "esc back  q quit",
                                   "\xe2\x86\x91\xe2\x86\x93 移动  \xe2\x86\xb5 执行  "
                                   "esc 返回  q 退出"));
  /* place cursor at the filter */
  if (m->filter_on) {
    tui_at(b, T.h - 2, 11 + gg_width(m->filter));
    sb_adds(b, CUR_SHOW);
  }
  tui_flush(b);
}

int tui_menu_key(tui_menu *m, int key) {
  int total = menu_filtered_count(m);
  int nth = menu_nth_of(m, m->sel);
  switch (key) {
    case KEY_UP:
      if (nth > 0) nth--;
      break;
    case KEY_DOWN:
      if (nth + 1 < total) nth++;
      break;
    case KEY_HOME:
      nth = 0;
      break;
    case KEY_END:
      nth = total ? total - 1 : 0;
      break;
    case KEY_PGUP:
      nth -= (T.h - 6);
      if (nth < 0) nth = 0;
      break;
    case KEY_PGDN:
      nth += (T.h - 6);
      if (nth >= total) nth = total ? total - 1 : 0;
      break;
    case KEY_BACKSPACE:
      if (m->filter[0]) {
        size_t n = strlen(m->filter);
        n = gg_utf8_prev(m->filter, n);
        m->filter[n] = 0;
      }
      break;
    case KEY_ENTER:
      if (total) return 1;
      return 0;
    default:
      if (key >= 32 && key != KEY_BACKSPACE && key < KEY_UP) {
        size_t n = strlen(m->filter);
        if (n < sizeof(m->filter) - 5) {
          char tmp[4];
          int k = gg_utf8_encode((uint32_t)key, tmp);
          if (n + (size_t)k < sizeof(m->filter) - 1) {
            memcpy(m->filter + n, tmp, (size_t)k);
            m->filter[n + (size_t)k] = 0;
          }
        }
      }
      break;
  }
  m->sel = menu_filtered_index(m, nth);
  if (m->sel < 0) m->sel = 0;
  return 0;
}

int tui_menu_run(tui_menu *m, int *other_key) {
  if (other_key) *other_key = 0;
  if (!T.interactive) {
    /* numbered fallback */
    for (int i = 0; i < m->n; i++) printf("  %2d) %s\n", i + 1, m->items[i]);
    printf("%s [1-%d]: ", m->title ? m->title : "select", m->n);
    fflush(stdout);
    char line[64];
    if (!fgets(line, sizeof(line), stdin)) return -1;
    int v = atoi(line);
    if (v >= 1 && v <= m->n) return v - 1;
    for (int i = 0; i < m->n; i++)
      if (gg_streq(gg_trim(line), m->items[i])) return i;
    return -1;
  }
  tui_enter();
  for (;;) {
    tui_menu_render(m);
    int k = tui_key(-1);
    if (k == KEY_NONE) continue;
    if (k == KEY_ESC) {
      if (m->filter[0]) {
        m->filter[0] = 0;
        continue;
      }
      tui_leave();
      return -1;
    }
    if (k == 'q' && !m->filter[0]) {
      tui_leave();
      return -1;
    }
    if (k == KEY_ENTER) {
      int total = menu_filtered_count(m);
      if (!total) continue;
      tui_leave();
      return m->sel;
    }
    if (k == KEY_TAB || k == KEY_SHIFT_TAB) {
      m->filter_on = !m->filter_on;
      continue;
    }
    if (!m->filter_on) {
      if (other_key) *other_key = k;
      tui_leave();
      return -2;
    }
    tui_menu_key(m, k);
  }
}

/* ------------------------------------------------------------------ */
/* line editor (shared by prompt + textbox)                            */
/* ------------------------------------------------------------------ */

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
  size_t cur; /* byte offset */
  int row, col; /* screen position of buffer start */
  int wrap;
} editor;

static editor ed_global;

static void ed_init(char *buf, size_t cap, size_t len, int row, int col,
                    int wrap) {
  ed_global.buf = buf;
  ed_global.cap = cap;
  ed_global.len = len;
  ed_global.cur = len;
  ed_global.row = row;
  ed_global.col = col;
  ed_global.wrap = wrap;
}

static void ed_insert(editor *e, uint32_t cp) {
  char tmp[4];
  int k = gg_utf8_encode(cp, tmp);
  if (e->len + (size_t)k + 1 > e->cap) return;
  memmove(e->buf + e->cur + k, e->buf + e->cur, e->len - e->cur);
  memcpy(e->buf + e->cur, tmp, (size_t)k);
  e->cur += (size_t)k;
  e->len += (size_t)k;
  e->buf[e->len] = 0;
}

static void ed_backspace(editor *e) {
  if (!e->cur) return;
  size_t prev = gg_utf8_prev(e->buf, e->cur);
  size_t n = e->cur - prev;
  memmove(e->buf + prev, e->buf + prev + n, e->len - e->cur);
  e->cur = prev;
  e->len -= n;
  e->buf[e->len] = 0;
}

static void ed_delete(editor *e) {
  if (e->cur >= e->len) return;
  size_t next = e->cur + 1;
  while (next < e->len && ((unsigned char)e->buf[next] & 0xC0) == 0x80) next++;
  memmove(e->buf + e->cur, e->buf + next, e->len - next);
  e->len -= next - e->cur;
  e->buf[e->len] = 0;
}

/* line/column helpers for the multiline editor */
static size_t line_start(editor *e, size_t pos) {
  while (pos > 0 && e->buf[pos - 1] != '\n') pos--;
  return pos;
}

static size_t line_end(editor *e, size_t pos) {
  while (pos < e->len && e->buf[pos] != '\n') pos++;
  return pos;
}

static void cursor_rc(editor *e, size_t pos, int *row, int *col) {
  size_t ls = line_start(e, pos);
  char *tmp = gg_strndup(e->buf + ls, pos - ls);
  *col = gg_width(tmp);
  free(tmp);
  *row = 0;
  for (size_t i = 0; i < ls; i++)
    if (e->buf[i] == '\n') (*row)++;
}

/* ------------------------------------------------------------------ */
/* prompt (single line)                                                */
/* ------------------------------------------------------------------ */

static void render_prompt(const char *title, const char *label, editor *e,
                          const char *help) {
  sbuf *b = &G_FRAME;
  tui_size();
  tui_clear(b);
  draw_header(b, title, help);
  tui_at(b, 4, 3);
  sb_adds(b, tui_style(ST_BOLD));
  sb_adds(b, label);
  sb_adds(b, ST_RESET);
  int lw = gg_width(label);
  int base_col = 3 + lw + 1;
  tui_at(b, 4, base_col);
  sb_adds(b, "\033[K");
  sb_adds(b, tui_style(ST_ACCENT));
  sb_adds(b, e->buf);
  sb_adds(b, ST_RESET);
  draw_footer(b, gg_tr("enter ok  esc cancel  ctrl-u clear",
                       "回车 确定  esc 取消  ctrl-u 清空"));
  char *pre = gg_strndup(e->buf, e->cur);
  int w = gg_width(pre);
  free(pre);
  tui_at(b, 4, base_col + w);
  sb_adds(b, CUR_SHOW);
  tui_flush(b);
}

int tui_prompt(const char *title, const char *label, char *buf, size_t cap,
               const char *help) {
  if (!T.interactive) {
    printf("%s%s: ", label ? label : "", help ? "" : "");
    fflush(stdout);
    if (!buf) return -1;
    if (!fgets(buf, (int)cap, stdin)) return -1;
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return *buf ? 0 : -1;
  }
  tui_enter();
  size_t len = strlen(buf);
  ed_init(buf, cap, len, 4, 3, 0);
  cursor_rc(&ed_global, len, &ed_global.row, &ed_global.col);
  for (;;) {
    render_prompt(title, label, &ed_global, help);
    int k = tui_key(-1);
    editor *e = &ed_global;
    switch (k) {
      case KEY_NONE: break;
      case KEY_ESC: tui_leave(); return -1;
      case KEY_ENTER: tui_leave(); return 0;
      case KEY_BACKSPACE: ed_backspace(e); break;
      case KEY_DEL: ed_delete(e); break;
      case KEY_LEFT:
        if (e->cur) e->cur = gg_utf8_prev(e->buf, e->cur);
        break;
      case KEY_RIGHT:
        if (e->cur < e->len) {
          size_t n = e->cur + 1;
          while (n < e->len && ((unsigned char)e->buf[n] & 0xC0) == 0x80) n++;
          e->cur = n;
        }
        break;
      case KEY_HOME: e->cur = 0; break;
      case KEY_END: e->cur = e->len; break;
      case 21: /* ctrl-u */
        e->cur = e->len = 0;
        e->buf[0] = 0;
        break;
      case 1: /* ctrl-a */
        e->cur = 0;
        break;
      case 5: /* ctrl-e */
        e->cur = e->len;
        break;
      default:
        if (k >= 32 && k != KEY_BACKSPACE && k < KEY_UP) ed_insert(e, (uint32_t)k);
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* confirm / select / message                                          */
/* ------------------------------------------------------------------ */

static void wrap_text(sbuf *b, const char *text, int width, const char *style) {
  const char *p = text;
  while (*p) {
    const char *nl = strchr(p, '\n');
    size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
    /* wrap by display width */
    size_t start = 0;
    while (start < linelen) {
      size_t take = linelen - start;
      char *chunk = gg_strndup(p + start, take);
      while (take > 1 && gg_width(chunk) > width) {
        size_t cut = strlen(chunk);
        cut = gg_utf8_prev(chunk, cut);
        chunk[cut] = 0;
        take = cut;
      }
      if (take < linelen - start) {
        /* try to break on a space for latin text */
        char *sp = strrchr(chunk, ' ');
        if (sp && (size_t)(sp - chunk) > (size_t)(width / 2)) {
          take = (size_t)(sp - chunk);
          chunk[take] = 0;
        }
      }
      if (!take) take = 1;
      free(chunk);
      char *seg = gg_strndup(p + start, take);
      if (style) sb_adds(b, style);
      sb_adds(b, seg);
      sb_adds(b, ST_RESET);
      sb_adds(b, "\r\n");
      free(seg);
      start += take;
    }
    if (!nl) break;
    if (linelen == 0) sb_adds(b, "\r\n");
    p = nl + 1;
  }
}

int tui_confirm(const char *title, const char *msg, int def) {
  if (!T.interactive) {
    int yes = def;
    const char *a = getenv("GG_ASSUME_YES");
    if (a && *a && !gg_streq(a, "0")) yes = 1;
    printf("%s%s %s\n", msg, yes ? gg_tr(" [yes]", " [是]") : gg_tr(" [no]", " [否]"),
           "");
    return yes;
  }
  tui_enter();
  const char *items[2];
  items[0] = gg_tr("yes", "是");
  items[1] = gg_tr("no", "否");
  int sel = def ? 0 : 1;
  for (;;) {
    sbuf *b = &G_FRAME;
    tui_clear(b);
    draw_header(b, title, 0);
    int row = 4;
    sb_adds(b, "");
    tui_at(b, row, 3);
    wrap_text(b, msg, T.w - 6, tui_style(ST_BOLD));
    row += 3;
    for (int i = 0; i < 2; i++) {
      tui_at(b, row + i, 5);
      if (i == sel) {
        sb_adds(b, tui_style(ST_SEL));
        sb_addf(b, " %s ", items[i]);
        sb_adds(b, ST_RESET);
      } else {
        sb_adds(b, tui_style(ST_MUTED));
        sb_addf(b, " %s ", items[i]);
        sb_adds(b, ST_RESET);
      }
    }
    draw_footer(b, gg_tr("y/n  enter confirm  esc cancel", "y/n  回车确认  esc 取消"));
    tui_flush(b);
    int k = tui_key(-1);
    if (k == 'y' || k == 'Y') {
      tui_leave();
      return 1;
    }
    if (k == 'n' || k == 'N' || k == KEY_ESC) {
      tui_leave();
      return 0;
    }
    if (k == KEY_ENTER) {
      tui_leave();
      return sel == 0;
    }
    if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_TAB) sel = !sel;
  }
}

int tui_select(const char *title, const char *msg, const char **items, int n) {
  if (!T.interactive) {
    printf("%s\n", msg ? msg : title);
    for (int i = 0; i < n; i++) printf("  %d) %s\n", i + 1, items[i]);
    printf("select [1-%d]: ", n);
    fflush(stdout);
    char line[64];
    if (!fgets(line, sizeof(line), stdin)) return -1;
    int v = atoi(line);
    return (v >= 1 && v <= n) ? v - 1 : -1;
  }
  tui_menu m;
  char titlebuf[256];
  if (msg && *msg)
    snprintf(titlebuf, sizeof(titlebuf), "%s — %s", title ? title : "", msg);
  else
    snprintf(titlebuf, sizeof(titlebuf), "%s", title ? title : "");
  tui_menu_init(&m, titlebuf, items, n);
  m.filter_on = 0;
  return tui_menu_run(&m, 0);
}

void tui_message(const char *title, const char *body) {
  if (!T.interactive) {
    if (title) printf("%s\n", title);
    if (body) printf("%s\n", body);
    return;
  }
  tui_enter();
  /* pager with scroll */
  char *text = gg_strdup(body ? body : "");
  int nlines = 1;
  for (char *p = text; *p; p++)
    if (*p == '\n') nlines++;
  /* split into wrapped display lines */
  strvec lines;
  sv_init(&lines);
  char *p = text;
  while (*p || *p == 0) {
    char *nl = strchr(p, '\n');
    size_t n = nl ? (size_t)(nl - p) : strlen(p);
    char *line = gg_strndup(p, n);
    /* wrap */
    size_t start = 0, len = strlen(line);
    int first = 1;
    while (start < len || first) {
      size_t take = len - start;
      char *chunk = gg_strndup(line + start, take);
      while (take > 1 && gg_width(chunk) > T.w - 6) {
        size_t cut = gg_utf8_prev(chunk, strlen(chunk));
        chunk[cut] = 0;
        take = cut;
      }
      if (!take && !first) break;
      char *seg = gg_strndup(line + start, take);
      sv_push(&lines, seg);
      free(seg);
      free(chunk);
      start += take;
      first = 0;
      if (!nl && start >= len) break;
    }
    free(line);
    if (!nl) break;
    p = nl + 1;
  }
  free(text);
  int scroll = 0;
  for (;;) {
    sbuf *b = &G_FRAME;
    tui_clear(b);
    draw_header(b, title, 0);
    int rows = T.h - 4;
    for (int i = 0; i < rows; i++) {
      int idx = scroll + i;
      tui_at(b, 3 + i, 3);
      sb_adds(b, "\033[K");
      if (idx >= 0 && idx < lines.n) sb_adds(b, lines.v[idx]);
    }
    draw_footer(b, gg_tr("up/down scroll  q close", "上下滚动  q 关闭"));
    tui_flush(b);
    int k = tui_key(-1);
    if (k == 'q' || k == KEY_ESC || k == KEY_ENTER) break;
    if (k == KEY_DOWN || k == 'j') scroll++;
    if (k == KEY_UP || k == 'k') scroll--;
    if (k == KEY_PGDN || k == ' ') scroll += rows;
    if (k == KEY_PGUP) scroll -= rows;
    if (k == KEY_HOME || k == 'g') scroll = 0;
    if (k == KEY_END || k == 'G') scroll = lines.n - 1;
    if (scroll < 0) scroll = 0;
    if (scroll > lines.n - rows && lines.n > rows) scroll = lines.n - rows;
  }
  sv_free(&lines);
  tui_leave();
}

/* ------------------------------------------------------------------ */
/* multiline textbox (script editor)                                   */
/* ------------------------------------------------------------------ */

static int is_kw(const char *s, size_t n) {
  static const char *kws[] = {
      "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
      "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
      "then", "true", "until", "while", 0};
  for (int i = 0; kws[i]; i++) {
    if (strlen(kws[i]) == n && memcmp(kws[i], s, n) == 0) return 1;
  }
  return 0;
}

/* minimal Lua highlighter used by the editor + preview panes */
void gg_highlight_lua(sbuf *out, const char *line, int col);
void gg_highlight_lua(sbuf *out, const char *line, int col) {
  (void)col;
  size_t i = 0, n = strlen(line);
  while (i < n) {
    char c = line[i];
    if (c == '-' && i + 1 < n && line[i + 1] == '-') {
      sb_adds(out, "\033[38;5;245m");
      sb_adds(out, line + i);
      sb_adds(out, ST_RESET);
      return;
    }
    if (c == '"' || c == '\'') {
      char q = c;
      size_t j = i + 1;
      while (j < n) {
        if (line[j] == '\\') j += 2;
        else if (line[j] == q) {
          j++;
          break;
        } else j++;
      }
      sb_adds(out, "\033[38;5;186m");
      sb_addn(out, line + i, j - i);
      sb_adds(out, ST_RESET);
      i = j;
      continue;
    }
    if ((c >= '0' && c <= '9') &&
        (i == 0 || !(isalnum((unsigned char)line[i - 1]) || line[i - 1] == '_'))) {
      size_t j = i;
      while (j < n && (isdigit((unsigned char)line[j]) || line[j] == '.' ||
                       line[j] == 'x' || line[j] == 'a' || line[j] == 'b' ||
                       line[j] == 'c' || line[j] == 'd' || line[j] == 'e' ||
                       line[j] == 'f'))
        j++;
      sb_adds(out, "\033[38;5;222m");
      sb_addn(out, line + i, j - i);
      sb_adds(out, ST_RESET);
      i = j;
      continue;
    }
    if (isalpha((unsigned char)c) || c == '_') {
      size_t j = i;
      while (j < n && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
      if (is_kw(line + i, j - i)) {
        sb_adds(out, "\033[1;38;5;81m");
        sb_addn(out, line + i, j - i);
        sb_adds(out, ST_RESET);
      } else {
        sb_addn(out, line + i, j - i);
      }
      i = j;
      continue;
    }
    if (c == ':' && i + 1 < n && line[i + 1] == ':') {
      sb_adds(out, "\033[38;5;81m");
      sb_adds(out, "::");
      sb_adds(out, ST_RESET);
      i += 2;
      continue;
    }
    if (strchr("+-*/%#=<>~(){}[];,.:", c)) {
      sb_adds(out, "\033[38;5;245m");
      sb_addc(out, c);
      sb_adds(out, ST_RESET);
      i++;
      continue;
    }
    sb_addc(out, c);
    i++;
  }
}

static void textbox_status(editor *e, sbuf *b, const char *fname, int modified) {
  tui_size();
  tui_at(b, T.h - 1, 1);
  sb_adds(b, "\033[K");
  sb_adds(b, " ");
  sb_adds(b, tui_style(ST_MUTED));
  int row, col;
  cursor_rc(e, e->cur, &row, &col);
  sb_addf(b, "%s%s  %d:%d  %zu bytes%s", fname ? fname : "",
          modified ? gg_tr("  [modified]", "  [已修改]") : "", row + 1, col + 1,
          e->len, "");
  sb_adds(b, ST_RESET);
  sb_adds(b, "   ");
  sb_adds(b, tui_style(ST_DIM));
  sb_adds(b, gg_tr("^s save  esc cancel  ^g help", "^s 保存  esc 取消  ^g 帮助"));
  sb_adds(b, ST_RESET);
}

char *tui_textbox(const char *title, const char *filename, const char *text) {
  if (!T.interactive) {
    /* non interactive: return a copy unchanged */
    return gg_strdup(text ? text : "");
  }
  size_t len = text ? strlen(text) : 0;
  size_t cap = len + 4096;
  char *buf = malloc(cap);
  memcpy(buf, text ? text : "", len + 1);
  tui_enter();
  editor *e = &ed_global;
  ed_init(buf, cap, len, 3, 1, 1);
  int modified = 0;
  int top = 0;
  int quit_try = 0;
  for (;;) {
    tui_size();
    sbuf *b = &G_FRAME;
    tui_clear(b);
    draw_header(b, title, filename);
    int rows = T.h - 3;
    int currow, curcol;
    cursor_rc(e, e->cur, &currow, &curcol);
    if (currow < top) top = currow;
    if (currow >= top + rows) top = currow - rows + 1;
    /* draw visible lines */
    size_t pos = 0;
    int lineno = 0;
    while (lineno < top) {
      size_t es = line_end(e, pos);
      if (es >= e->len && pos >= e->len) break;
      pos = es + 1;
      lineno++;
    }
    for (int r = 0; r < rows; r++) {
      tui_at(b, 3 + r, 1);
      sb_adds(b, "\033[K");
      if (pos > e->len) break;
      size_t es = line_end(e, pos);
      sb_adds(b, tui_style(ST_DIM));
      sb_addf(b, "%3d ", top + r + 1);
      sb_adds(b, ST_RESET);
      char *line = gg_strndup(e->buf + pos, es - pos);
      sb_adds(b, " ");
      gg_highlight_lua(b, line, 0);
      free(line);
      if (es >= e->len) {
        pos = e->len + 1;
        /* keep drawing empty lines until viewport bottom */
        for (int r2 = r + 1; r2 < rows; r2++) {
          tui_at(b, 3 + r2, 1);
          sb_adds(b, "\033[K");
          sb_adds(b, tui_style(ST_DIM));
          sb_addf(b, "%3d ", top + r2 + 1);
          sb_adds(b, ST_RESET);
        }
        break;
      }
      pos = es + 1;
    }
    textbox_status(e, b, filename, modified);
    /* cursor */
    int screen_row = 3 + (currow - top);
    int screen_col = 6 + curcol;
    if (screen_row >= 3 && screen_row < T.h - 1) {
      tui_at(b, screen_row, screen_col);
      sb_adds(b, CUR_SHOW);
    }
    tui_flush(b);
    int k = tui_key(-1);
    switch (k) {
      case KEY_NONE: break;
      case KEY_ESC: {
        if (modified && !quit_try) {
          quit_try = 1;
          if (!tui_confirm(gg_tr("discard changes?", "放弃修改?"),
                           gg_tr("The buffer has unsaved changes.",
                                 "编辑缓冲区有未保存的修改。"),
                           0)) {
            break;
          }
        }
        free(buf);
        tui_leave();
        return 0;
      }
      case 19: /* ctrl-s */
        tui_leave();
        return buf;
      case 7: /* ctrl-g help */
        tui_message(gg_tr("editor help", "编辑器帮助"),
                    gg_tr("ctrl-s  save and return\n"
                          "esc     cancel\n"
                          "ctrl-a/e start/end of line\n"
                          "ctrl-k  delete line\n"
                          "ctrl-u  delete to line start\n"
                          "tab     insert two spaces",
                          "ctrl-s  保存并返回\n"
                          "esc     取消\n"
                          "ctrl-a/e 行首/行尾\n"
                          "ctrl-k  删除整行\n"
                          "ctrl-u  删除到行首\n"
                          "tab     插入两个空格"));
        tui_enter();
        break;
      case KEY_BACKSPACE: ed_backspace(e); modified = 1; break;
      case KEY_DEL: ed_delete(e); modified = 1; break;
      case KEY_ENTER:
        ed_insert(e, '\n');
        modified = 1;
        break;
      case KEY_TAB:
        ed_insert(e, ' ');
        ed_insert(e, ' ');
        modified = 1;
        break;
      case KEY_LEFT:
        if (e->cur) e->cur = gg_utf8_prev(e->buf, e->cur);
        break;
      case KEY_RIGHT:
        if (e->cur < e->len) {
          size_t n = e->cur + 1;
          while (n < e->len && ((unsigned char)e->buf[n] & 0xC0) == 0x80) n++;
          e->cur = n;
        }
        break;
      case KEY_HOME:
      case 1:
        e->cur = line_start(e, e->cur);
        break;
      case KEY_END:
      case 5:
        e->cur = line_end(e, e->cur);
        break;
      case KEY_UP: {
        size_t ls = line_start(e, e->cur);
        int col = curcol;
        if (ls == 0) break;
        size_t pls = line_start(e, ls - 1);
        size_t ple = line_end(e, pls);
        size_t p = pls;
        int w = 0;
        while (p < ple && w < col) {
          size_t nx = p + 1;
          while (nx < ple && ((unsigned char)e->buf[nx] & 0xC0) == 0x80) nx++;
          char tmp[8];
          memcpy(tmp, e->buf + p, nx - p);
          tmp[nx - p] = 0;
          w += gg_width(tmp);
          p = nx;
        }
        e->cur = p;
        break;
      }
      case KEY_DOWN: {
        size_t le = line_end(e, e->cur);
        if (le >= e->len) break;
        size_t nls = le + 1;
        size_t nle = line_end(e, nls);
        size_t p = nls;
        int w = 0;
        while (p < nle && w < curcol) {
          size_t nx = p + 1;
          while (nx < nle && ((unsigned char)e->buf[nx] & 0xC0) == 0x80) nx++;
          char tmp[8];
          memcpy(tmp, e->buf + p, nx - p);
          tmp[nx - p] = 0;
          w += gg_width(tmp);
          p = nx;
        }
        e->cur = p;
        break;
      }
      case 11: { /* ctrl-k */
        size_t le = line_end(e, e->cur);
        size_t end = le < e->len ? le + 1 : le;
        size_t n = end - e->cur;
        memmove(e->buf + e->cur, e->buf + end, e->len - end);
        e->len -= n;
        e->buf[e->len] = 0;
        modified = 1;
        break;
      }
      case 21: { /* ctrl-u */
        size_t ls = line_start(e, e->cur);
        size_t n = e->cur - ls;
        memmove(e->buf + ls, e->buf + e->cur, e->len - e->cur);
        e->len -= n;
        e->cur = ls;
        e->buf[e->len] = 0;
        modified = 1;
        break;
      }
      case KEY_PGUP:
        top -= (rows - 2);
        if (top < 0) top = 0;
        break;
      case KEY_PGDN:
        top += (rows - 2);
        break;
      default:
        if (k >= 32 && k < KEY_UP) {
          ed_insert(e, (uint32_t)k);
          modified = 1;
          quit_try = 0;
        }
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* forms                                                               */
/* ------------------------------------------------------------------ */

int tui_form(const char *title, const char *subtitle, tui_field *f, int n) {
  if (!T.interactive) return 0; /* keep defaults */
  int cur = 0;
  int scroll = 0;
  tui_enter();
  for (;;) {
    tui_size();
    sbuf *b = &G_FRAME;
    tui_clear(b);
    draw_header(b, title, subtitle);
    int rows = T.h - 4;
    if (cur < scroll) scroll = cur;
    if (cur >= scroll + rows) scroll = cur - rows + 1;
    for (int r = 0; r < rows; r++) {
      int i = scroll + r;
      tui_at(b, 3 + r, 1);
      sb_adds(b, "\033[K");
      if (i >= n) continue;
      sb_adds(b, " ");
      if (i == cur) sb_adds(b, tui_style(ST_SEL));
      else sb_adds(b, tui_style(ST_MUTED));
      sb_adds(b, b_mark);
      sb_adds(b, ST_RESET);
      sb_adds(b, " ");
      if (i == cur) sb_adds(b, tui_style(ST_BOLD));
      sb_adds(b, f[i].label ? f[i].label : f[i].name);
      sb_adds(b, ST_RESET);
      sb_adds(b, "  ");
      if (f[i].kind == 1) {
        sb_addf(b, "[%s]", (f[i].value && gg_streq(f[i].value, "1")) ? "x" : " ");
      } else if (f[i].kind == 2) {
        sb_adds(b, tui_style(ST_ACCENT));
        sb_addf(b, "%s <", f[i].value ? f[i].value : "");
        sb_adds(b, ST_RESET);
        sb_addf(b, " %s/%d", "", f[i].nchoices);
      } else {
        sb_adds(b, tui_style(ST_ACCENT));
        sb_adds(b, f[i].value ? f[i].value : "");
        sb_adds(b, ST_RESET);
      }
      if (f[i].help && i == cur) {
        sb_adds(b, tui_style(ST_MUTED));
        sb_addf(b, "   %s", f[i].help);
        sb_adds(b, ST_RESET);
      }
    }
    draw_footer(b, gg_tr("tab/enter next  space toggle  esc back  ctrl-s submit",
                         "tab/回车 下一个  空格 切换   esc 返回  ctrl-s 执行"));
    tui_flush(b);
    int k = tui_key(-1);
    if (k == KEY_ESC) {
      tui_leave();
      return -1;
    }
    if (k == 19 || k == KEY_F1) { /* ctrl-s */
      tui_leave();
      return 0;
    }
    if (k == KEY_DOWN || k == KEY_TAB || k == KEY_ENTER) {
      if (cur + 1 < n) cur++;
      else {
        tui_leave();
        return 0;
      }
      continue;
    }
    if (k == KEY_UP || k == KEY_SHIFT_TAB) {
      if (cur > 0) cur--;
      continue;
    }
    if (k == KEY_LEFT || k == KEY_RIGHT) {
      if (f[cur].kind == 1) {
        f[cur].value = gg_strdup(f[cur].value && gg_streq(f[cur].value, "1") ? "0" : "1");
      } else if (f[cur].kind == 2 && f[cur].nchoices > 0) {
        int idx = 0;
        for (int c = 0; c < f[cur].nchoices; c++)
          if (f[cur].value && gg_streq(f[cur].value, f[cur].choices[c])) idx = c;
        idx = (idx + (k == KEY_RIGHT ? 1 : f[cur].nchoices - 1)) % f[cur].nchoices;
        free(f[cur].value);
        f[cur].value = gg_strdup(f[cur].choices[idx]);
      }
      continue;
    }
    if ((k == ' ' || k == 'x') && f[cur].kind == 1) {
      f[cur].value = gg_strdup(f[cur].value && gg_streq(f[cur].value, "1") ? "0" : "1");
      continue;
    }
    if (k == KEY_BACKSPACE) {
      if (f[cur].kind == 0 && f[cur].value && *f[cur].value) {
        size_t len = strlen(f[cur].value);
        f[cur].value[gg_utf8_prev(f[cur].value, len)] = 0;
      }
      continue;
    }
    if (k >= 32 && k < KEY_UP && f[cur].kind == 0) {
      size_t len = f[cur].value ? strlen(f[cur].value) : 0;
      char tmp[4];
      int nb = gg_utf8_encode((uint32_t)k, tmp);
      char *nv = malloc(len + (size_t)nb + 1);
      memcpy(nv, f[cur].value ? f[cur].value : "", len);
      memcpy(nv + len, tmp, (size_t)nb);
      nv[len + (size_t)nb] = 0;
      free(f[cur].value);
      f[cur].value = nv;
      continue;
    }
  }
}

/* ------------------------------------------------------------------ */
/* progress                                                            */
/* ------------------------------------------------------------------ */

static struct {
  const char *title, *subtitle;
  double pct;
  char msg[256];
  strvec log;
  int active;
} P;

void tui_progress_begin(const char *title, const char *subtitle) {
  P.title = title;
  P.subtitle = subtitle;
  P.pct = -1;
  P.msg[0] = 0;
  sv_init(&P.log);
  P.active = 1;
  if (T.interactive) tui_enter();
}

void tui_progress_log(const char *line) {
  if (!P.active) return;
  char *clean = gg_strdup(line ? line : "");
  /* strip CR and ANSI */
  sbuf c;
  sb_init(&c, 128);
  size_t n = strlen(clean);
  for (size_t i = 0; i < n; i++) {
    if (clean[i] == '\r') continue;
    if (clean[i] == 0x1B) {
      i++;
      if (i < n && clean[i] == '[') {
        i++;
        while (i < n && !((unsigned char)clean[i] >= 0x40 && (unsigned char)clean[i] <= 0x7E))
          i++;
      }
      continue;
    }
    sb_addc(&c, clean[i]);
  }
  free(clean);
  char *text = gg_strdup(c.p);
  sb_free(&c);
  sv_push(&P.log, text);
  free(text);
  if (P.log.n > 500) {
    free(P.log.v[0]);
    memmove(P.log.v, P.log.v + 1, (size_t)(P.log.n - 1) * sizeof(char *));
    P.log.n--;
  }
}

void tui_progress_set(double pct, const char *msg) {
  P.pct = pct;
  if (msg) snprintf(P.msg, sizeof(P.msg), "%s", msg);
  if (!T.interactive) {
    if (msg) printf("%s\n", msg);
    return;
  }
  sbuf *b = &G_FRAME;
  tui_size();
  tui_clear(b);
  draw_header(b, P.title, P.subtitle);
  int barw = T.w - 30;
  if (barw > 60) barw = 60;
  if (barw < 10) barw = 10;
  tui_at(b, 4, 3);
  sb_adds(b, tui_style(ST_BOLD));
  sb_adds(b, " [");
  sb_adds(b, ST_RESET);
  if (pct < 0) {
    sb_adds(b, tui_style(ST_ACCENT));
    for (int i = 0; i < barw; i++) sb_adds(b, i == (int)(time(0) * 8) % barw ? "\u2588" : b_h);
    sb_adds(b, ST_RESET);
  } else {
    int filled = (int)(pct * barw / 100.0 + 0.5);
    if (filled > barw) filled = barw;
    sb_adds(b, tui_style(ST_OK));
    for (int i = 0; i < filled; i++) sb_adds(b, "\u2588");
    sb_adds(b, tui_style(ST_DIM));
    for (int i = filled; i < barw; i++) sb_adds(b, b_h);
    sb_adds(b, ST_RESET);
  }
  tui_at(b, 4, 5 + barw);
  sb_adds(b, tui_style(ST_BOLD));
  if (pct >= 0) sb_addf(b, " %5.1f%%", pct);
  else sb_adds(b, "   ...");
  sb_adds(b, ST_RESET);
  tui_at(b, 5, 3);
  sb_adds(b, "\033[K");
  sb_adds(b, tui_style(ST_ACCENT));
  sb_adds(b, P.msg);
  sb_adds(b, ST_RESET);
  int rows = T.h - 8;
  if (rows < 1) rows = 1;
  int start = P.log.n - rows;
  if (start < 0) start = 0;
  for (int i = 0; i < rows; i++) {
    int idx = start + i;
    tui_at(b, 7 + i, 3);
    sb_adds(b, "\033[K");
    if (idx < P.log.n) {
      char *l = P.log.v[idx];
      int w = gg_width(l);
      if (w > T.w - 5) {
        /* keep the tail of long lines (progress bars live there) */
        size_t len = strlen(l);
        while (len && gg_width(l) > T.w - 5) l++, len--;
      }
      sb_adds(b, tui_style(ST_MUTED));
      sb_adds(b, l);
      sb_adds(b, ST_RESET);
    }
  }
  draw_footer(b, gg_tr("working... ctrl-c to cancel", "运行中... ctrl-c 取消"));
  tui_flush(b);
}

void tui_progress_end(int ok, const char *msg) {
  if (!P.active) return;
  if (T.interactive) {
    sbuf *b = &G_FRAME;
    tui_size();
    tui_clear(b);
    draw_header(b, P.title, P.subtitle);
    tui_at(b, 4, 3);
    sb_adds(b, ok ? tui_style(ST_OK) : tui_style(ST_ERR));
    sb_adds(b, ok ? gg_tr("done. ", "完成。") : gg_tr("failed. ", "失败。"));
    sb_adds(b, tui_style(ST_BOLD));
    sb_adds(b, msg ? msg : "");
    sb_adds(b, ST_RESET);
    int rows = T.h - 7;
    if (rows < 1) rows = 1;
    int start = P.log.n - rows;
    if (start < 0) start = 0;
    for (int i = 0; i < rows; i++) {
      int idx = start + i;
      tui_at(b, 6 + i, 3);
      sb_adds(b, "\033[K");
      if (idx < P.log.n) {
        sb_adds(b, tui_style(ST_MUTED));
        sb_adds(b, P.log.v[idx]);
        sb_adds(b, ST_RESET);
      }
    }
    draw_footer(b, gg_tr("press any key to continue", "按任意键继续"));
    tui_flush(b);
    sv_free(&P.log);
    P.active = 0;
    if (!ok) {
      tui_key(-1);
    } else {
      /* brief pause so the user can see the result */
      tui_key(350);
    }
    tui_leave();
  } else {
    printf("%s%s\n", ok ? "" : "failed: ", msg ? msg : "");
    sv_free(&P.log);
    P.active = 0;
  }
}
