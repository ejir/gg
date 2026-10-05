/* gg — self-contained scripts, redbean style.
 *
 * A zip archive appended to the executable carries the user's own files:
 *
 *   modules/<name>.lua   modules that `gg <name>` can run from any machine
 *   assets/<...>         data files a script can read (gg.assets.*)
 *   init.lua             startup hook, run once when the lua state comes up
 *   registry.tsv         plain `gg add` one liners
 *
 * `gg bundle` writes such a copy; everybody else only needs to read it.
 * Cosmopolitan's zipos gives us deflate + /zip/ paths for free on the APE;
 * the portable reader below handles the stored entries gg itself writes,
 * which is what the host build and the tests use.
 *
 * Layout notes (same convention as Info-ZIP and python's zipfile):
 *   - offsets in the central directory are absolute positions in the file;
 *   - appending means: keep everything up to the old central directory,
 *     write the new local entries there, rebuild the central directory and
 *     put a fresh EOCD at the very end.  zip_start (the prefix in front of
 *     the archive) is zero, so the numbers stay obvious.
 */
#include "gg.h"

#define ZIP_LOCAL_SIG 0x04034b50u
#define ZIP_CDIR_SIG 0x02014b50u
#define ZIP_EOCD_SIG 0x06054b50u
#define ZIP_TAIL_MAX 66560u /* 64k comment + eocd */

typedef struct {
  char *name;
  char *extra; /* central directory extra field, copied verbatim */
  char *comment;
  unsigned int extra_len, comment_len;
  unsigned short version_made, version_need, flags, method;
  unsigned short time, date, disk, iattr;
  unsigned int crc, csize, usize, eattr, off; /* off: absolute local header */
} zip_rec;

static zip_rec *g_rec;
static int g_n, g_cap, g_scanned, g_present;
static char *g_src_path; /* the file we scanned */
static char **g_user;    /* names listed in .gg-manifest */
static int g_user_n = -1;
static int g_have_manifest;

/* ------------------------------------------------------------------ */
/* crc32                                                               */
/* ------------------------------------------------------------------ */

static unsigned int crc_table[256];
static int crc_ready;

static void crc_init(void) {
  for (unsigned int i = 0; i < 256; i++) {
    unsigned int c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
    crc_table[i] = c;
  }
  crc_ready = 1;
}

unsigned int gg_crc32(const unsigned char *p, size_t n) {
  if (!crc_ready) crc_init();
  unsigned int c = 0xffffffffu;
  for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

/* ------------------------------------------------------------------ */
/* little endian helpers                                               */
/* ------------------------------------------------------------------ */

static unsigned int rd16(const unsigned char *p) {
  return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int rd32(const unsigned char *p) {
  return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
         ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void wr16(sbuf *b, unsigned int v) {
  char t[3];
  t[0] = (char)(v & 0xff);
  t[1] = (char)((v >> 8) & 0xff);
  t[2] = 0;
  sb_addn(b, t, 2);
}

static void wr32(sbuf *b, unsigned int v) {
  char t[5];
  t[0] = (char)(v & 0xff);
  t[1] = (char)((v >> 8) & 0xff);
  t[2] = (char)((v >> 16) & 0xff);
  t[3] = (char)((v >> 24) & 0xff);
  t[4] = 0;
  sb_addn(b, t, 4);
}

/* ------------------------------------------------------------------ */
/* reading the archive that is glued to this binary                     */
/* ------------------------------------------------------------------ */

static void rec_free(zip_rec *r) {
  free(r->name);
  free(r->extra);
  free(r->comment);
}

static void bundle_clear(void) {
  for (int i = 0; i < g_n; i++) rec_free(&g_rec[i]);
  g_n = 0;
  g_present = 0;
  free(g_src_path);
  g_src_path = 0;
  for (int i = 0; i < g_user_n && g_user; i++) free(g_user[i]);
  free(g_user);
  g_user = 0;
  g_user_n = -1;
  g_have_manifest = 0;
}

/* keep the (possibly in-memory) archive of `path` in g_rec */
static int bundle_parse(const char *path, const unsigned char *data,
                        size_t size) {
  if (size < 22) return 0;
  size_t scan_from = size > ZIP_TAIL_MAX ? size - ZIP_TAIL_MAX : 0;
  size_t eocd = 0;
  for (size_t i = size - 22 + 1; i-- > scan_from;) {
    if (rd32(data + i) == ZIP_EOCD_SIG) {
      eocd = i;
      break;
    }
    if (i == scan_from) break;
  }
  if (!eocd) return 0;
  unsigned int count = rd16(data + eocd + 10);
  unsigned int cdsize = rd32(data + eocd + 12);
  unsigned int cdoff = rd32(data + eocd + 16);
  /* offsets may be relative to the archive start (self extracting files) */
  if (!cdoff || cdoff + 4 > size) return 0;
  long zip_start = (long)eocd - (long)cdoff - (long)cdsize;
  if (zip_start < 0) zip_start = 0;
  size_t cd = (size_t)zip_start + cdoff;
  if (cd + 4 > size || rd32(data + cd) != ZIP_CDIR_SIG) {
    zip_start = 0; /* assume the plain "absolute offsets" case */
    cd = cdoff;
    if (cd + 4 > size || rd32(data + cd) != ZIP_CDIR_SIG) return 0;
  }
  free(g_src_path);
  g_src_path = gg_strdup(path);
  size_t p = cd;
  for (unsigned int i = 0; i < count; i++) {
    if (p + 46 > size || rd32(data + p) != ZIP_CDIR_SIG) break;
    unsigned int namelen = rd16(data + p + 28);
    unsigned int extralen = rd16(data + p + 30);
    unsigned int cmtlen = rd16(data + p + 32);
    if (p + 46 + namelen + extralen + cmtlen > size) break;
    if (g_n + 1 > g_cap) {
      g_cap = g_cap ? g_cap * 2 : 32;
      g_rec = realloc(g_rec, (size_t)g_cap * sizeof(zip_rec));
    }
    zip_rec *r = &g_rec[g_n];
    memset(r, 0, sizeof(*r));
    r->version_made = (unsigned short)rd16(data + p + 4);
    r->version_need = (unsigned short)rd16(data + p + 6);
    r->flags = (unsigned short)rd16(data + p + 8);
    r->method = (unsigned short)rd16(data + p + 10);
    r->time = (unsigned short)rd16(data + p + 12);
    r->date = (unsigned short)rd16(data + p + 14);
    r->crc = rd32(data + p + 16);
    r->csize = rd32(data + p + 20);
    r->usize = rd32(data + p + 24);
    r->disk = (unsigned short)rd16(data + p + 34);
    r->iattr = (unsigned short)rd16(data + p + 36);
    r->eattr = rd32(data + p + 38);
    r->off = rd32(data + p + 42) + (unsigned int)zip_start;
    r->name = gg_strndup((const char *)data + p + 46, namelen);
    if (extralen) r->extra = gg_strndup((const char *)data + p + 46 + namelen, extralen);
    if (cmtlen)
      r->comment =
          gg_strndup((const char *)data + p + 46 + namelen + extralen, cmtlen);
    r->extra_len = extralen;
    r->comment_len = cmtlen;
    g_n++;
    p += 46 + namelen + extralen + cmtlen;
  }
  g_present = g_n > 0;
  /* the manifest tells us which entries were added by `gg bundle` */
  g_have_manifest = 0;
  for (int i = 0; i < g_n; i++) {
    if (!gg_streq(g_rec[i].name, ".gg-manifest")) continue;
    g_have_manifest = 1;
    g_user_n = 0;
    size_t mlen = 0;
    char *m = bundle_read(".gg-manifest", &mlen);
    if (m) {
      char *p = m;
      while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        char *line = gg_trim(p);
        if (*line) {
          g_user = realloc(g_user, (size_t)(g_user_n + 1) * sizeof(char *));
          g_user[g_user_n++] = gg_strdup(line);
        }
        p = nl ? nl + 1 : 0;
      }
      free(m);
    }
    break;
  }
  return g_present;
}

int bundle_scan_file(const char *path) {
  size_t size = 0;
  char *data = gg_read_file(path, &size);
  if (!data) return 0;
  bundle_clear(); /* parse() appends: start from an empty list */
  int ok = bundle_parse(path, (const unsigned char *)data, size);
  free(data);
  g_scanned = 1;
  return ok;
}

int bundle_scan(void) {
  if (g_scanned) return g_present;
  g_scanned = 1;
  return bundle_scan_file(gg_exe_path());
}

int bundle_present(void) { return bundle_scan(); }

int bundle_count(void) {
  bundle_scan();
  return g_n;
}

const char *bundle_entry_name(int i) {
  bundle_scan();
  if (i < 0 || i >= g_n) return 0;
  return g_rec[i].name;
}

unsigned int bundle_entry_size(int i) {
  bundle_scan();
  if (i < 0 || i >= g_n) return 0;
  return g_rec[i].usize;
}

const char *bundle_path(void) {
  bundle_scan();
  return g_src_path;
}

const char *bundle_self(void) { return bundle_path(); }

char *bundle_read(const char *name, size_t *lenp) {
  bundle_scan();
  for (int i = 0; i < g_n; i++) {
    if (!gg_streq(g_rec[i].name, name)) continue;
    zip_rec *r = &g_rec[i];
    if (lenp) *lenp = r->usize;
    if (r->method == 0) { /* stored: read it ourselves, everywhere */
      FILE *f = fopen(g_src_path ? g_src_path : gg_exe_path(), "rb");
      if (!f) return 0;
      unsigned int namelen = 30 + (unsigned int)strlen(r->name);
      if (fseek(f, (long)r->off + namelen, SEEK_SET) != 0) {
        /* the local header may carry an extra field of its own */
        if (fseek(f, (long)r->off, SEEK_SET) != 0) {
          fclose(f);
          return 0;
        }
        unsigned char h[30];
        if (fread(h, 1, sizeof(h), f) != sizeof(h)) {
          fclose(f);
          return 0;
        }
        namelen = 30 + rd16(h + 26) + rd16(h + 28);
        if (fseek(f, (long)r->off + namelen, SEEK_SET) != 0) {
          fclose(f);
          return 0;
        }
      }
      char *buf = malloc((size_t)r->usize + 1);
      if (!buf) {
        fclose(f);
        return 0;
      }
      size_t got = fread(buf, 1, r->usize, f);
      fclose(f);
      buf[got] = 0;
      if (got != r->usize) {
        free(buf);
        return 0;
      }
      return buf;
    }
    if (r->method == 8) {
#if defined(__COSMOPOLITAN__)
      /* cosmopolitan inflates for us through zipos */
      char *zp = gg_asprintf("/zip/%s", name);
      size_t n = 0;
      char *out = gg_read_file(zp, &n);
      free(zp);
      if (out && lenp) *lenp = n;
      return out;
#else
      gg_warn("%s: %s", name,
              gg_tr("deflated asset — rebuild the bundle with `gg bundle`",
                    "压缩过的资源 — 用 `gg bundle` 重新打包"));
      return 0;
#endif
    }
  }
  return 0;
}

int bundle_has(const char *name) {
  bundle_scan();
  for (int i = 0; i < g_n; i++)
    if (gg_streq(g_rec[i].name, name)) return 1;
  return 0;
}

/* ------------------------------------------------------------------ */
/* writing a bundle                                                    */
/* ------------------------------------------------------------------ */

static void dos_stamp(unsigned short *t, unsigned short *d) {
  time_t now = time(0);
  struct tm *tm = localtime(&now);
  if (!tm) {
    *t = *d = 0;
    return;
  }
  *t = (unsigned short)((tm->tm_hour << 11) | (tm->tm_min << 5) |
                        (tm->tm_sec / 2));
  *d = (unsigned short)(((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) |
                        tm->tm_mday);
}

/* add one stored file to `out` and return its new central directory record */
static void add_stored(sbuf *out, const char *name, const char *data,
                       size_t len, unsigned int *count, sbuf *cd) {
  unsigned int crc = gg_crc32((const unsigned char *)data, len);
  unsigned int off = (unsigned int)out->n;
  unsigned short t, d;
  dos_stamp(&t, &d);
  sb_addf(out, "PK%c%c", 3, 4);
  wr16(out, 20);      /* version needed */
  wr16(out, 0x0800);  /* utf-8 names */
  wr16(out, 0);       /* stored */
  wr16(out, t);
  wr16(out, d);
  wr32(out, crc);
  wr32(out, (unsigned int)len);
  wr32(out, (unsigned int)len);
  wr16(out, (unsigned int)strlen(name));
  wr16(out, 0); /* extra */
  sb_addn(out, name, strlen(name));
  sb_addn(out, data, len);
  /* central directory record */
  sb_addf(cd, "PK%c%c", 1, 2);
  wr16(cd, 0x031e); /* made by unix, 3.0 */
  wr16(cd, 20);
  wr16(cd, 0x0800);
  wr16(cd, 0);
  wr16(cd, t);
  wr16(cd, d);
  wr32(cd, crc);
  wr32(cd, (unsigned int)len);
  wr32(cd, (unsigned int)len);
  wr16(cd, (unsigned int)strlen(name));
  wr16(cd, 0); /* extra */
  wr16(cd, 0); /* comment */
  wr16(cd, 0); /* disk */
  wr16(cd, 0); /* internal attrs */
  wr32(cd, 0100644u << 16);
  wr32(cd, off);
  sb_addn(cd, name, strlen(name));
  (*count)++;
}

/* re-emit a record we parsed earlier (offsets are already absolute) */
static void reemit_cd(sbuf *cd, const zip_rec *r) {
  sb_addf(cd, "PK%c%c", 1, 2);
  wr16(cd, r->version_made);
  wr16(cd, r->version_need);
  wr16(cd, r->flags);
  wr16(cd, r->method);
  wr16(cd, r->time);
  wr16(cd, r->date);
  wr32(cd, r->crc);
  wr32(cd, r->csize);
  wr32(cd, r->usize);
  wr16(cd, (unsigned int)strlen(r->name));
  wr16(cd, r->extra_len);
  wr16(cd, r->comment_len);
  wr16(cd, r->disk);
  wr16(cd, r->iattr);
  wr32(cd, r->eattr);
  wr32(cd, r->off);
  sb_addn(cd, r->name, strlen(r->name));
  if (r->extra_len) sb_addn(cd, r->extra, r->extra_len);
  if (r->comment_len) sb_addn(cd, r->comment, r->comment_len);
}

/* collect `dir` (recursively) as "prefix/relpath" -> file path pairs */
typedef struct {
  char **zipname;
  char **diskpath;
  int n, cap;
} bundle_files;

static void bf_add(bundle_files *bf, const char *zipname, const char *disk) {
  if (bf->n + 1 > bf->cap) {
    bf->cap = bf->cap ? bf->cap * 2 : 64;
    bf->zipname = realloc(bf->zipname, (size_t)bf->cap * sizeof(char *));
    bf->diskpath = realloc(bf->diskpath, (size_t)bf->cap * sizeof(char *));
  }
  bf->zipname[bf->n] = gg_strdup(zipname);
  bf->diskpath[bf->n] = gg_strdup(disk);
  bf->n++;
}

static void bf_free(bundle_files *bf) {
  for (int i = 0; i < bf->n; i++) {
    free(bf->zipname[i]);
    free(bf->diskpath[i]);
  }
  free(bf->zipname);
  free(bf->diskpath);
  memset(bf, 0, sizeof(*bf));
}

static void bf_scan_dir(bundle_files *bf, const char *dir, const char *prefix) {
  DIR *d = opendir(dir);
  if (!d) return;
  struct dirent *de;
  while ((de = readdir(d))) {
    if (gg_streq(de->d_name, ".") || gg_streq(de->d_name, "..")) continue;
    char *full = gg_join(dir, de->d_name);
    char *zipname = gg_asprintf("%s/%s", prefix, de->d_name);
    if (gg_is_dir(full))
      bf_scan_dir(bf, full, zipname);
    else if (gg_is_file(full))
      bf_add(bf, zipname, full);
    free(zipname);
    free(full);
  }
  closedir(d);
}

static int excluded(const char *name, const char *const *excl, int nexcl) {
  for (int i = 0; i < nexcl; i++) {
    if (gg_glob_match(excl[i], name)) return 1;
    char *deeper = gg_asprintf("%s/*", excl[i]);
    int hit = gg_glob_match(deeper, name);
    free(deeper);
    if (hit) return 1;
  }
  return 0;
}

/* the manifest records the entries gg itself added, so `gg.assets.list()`
 * and a later `gg bundle` only look at the user's files */
static void manifest_add(sbuf *m, const char *name) { sb_addf(m, "%s\n", name); }

int bundle_write(const char *out, const char *const *zipnames,
                 const char *const *diskpaths, int nfiles, int force,
                 const char *const *excl, int nexcl, char *err, size_t errsz) {
  if (!force && gg_exists(out)) {
    snprintf(err, errsz, "%s %s", out, gg_tr("exists (use -f)", "已存在（用 -f）"));
    return -1;
  }
  const char *self = gg_exe_path();
  size_t size = 0;
  char *data = gg_read_file(self, &size);
  if (!data) {
    snprintf(err, errsz, "%s: %s", self, strerror(errno));
    return -1;
  }
  /* where do we stop keeping bytes?  right at the old central directory */
  bundle_clear();
  bundle_parse(self, (const unsigned char *)data, size);
  size_t keep = size;
  if (g_n) {
    /* recompute the old central directory position */
    unsigned int cdoff = 0, cdsize = 0;
    size_t eocd = 0;
    for (size_t i = size - 22 + 1; i-- > (size > ZIP_TAIL_MAX ? size - ZIP_TAIL_MAX : 0);) {
      if (rd32((const unsigned char *)data + i) == ZIP_EOCD_SIG) {
        eocd = i;
        break;
      }
      if (i == 0) break;
    }
    if (eocd) {
      cdsize = rd32((const unsigned char *)data + eocd + 12);
      cdoff = rd32((const unsigned char *)data + eocd + 16);
      long zip_start = (long)eocd - (long)cdoff - (long)cdsize;
      if (zip_start < 0) zip_start = 0;
      keep = (size_t)zip_start + cdoff;
      if (keep > size) keep = size;
    }
    /* the records we re-emit must use absolute offsets */
    long zip_start = (long)eocd - (long)rd32((const unsigned char *)data + eocd + 16) -
                     (long)cdsize;
    if (zip_start < 0) zip_start = 0;
    for (int i = 0; i < g_n; i++) g_rec[i].off += (unsigned int)zip_start;
  }
  sbuf out_buf;
  sb_init(&out_buf, size + 4096);
  sb_addn(&out_buf, data, keep);
  free(data);
  sbuf cd;
  sb_init(&cd, 4096);
  sbuf manifest;
  sb_init(&manifest, 256);
  unsigned int count = 0;
  for (int i = 0; i < g_n; i++) {
    const char *nm = g_rec[i].name;
    if (gg_streq(nm, ".gg-manifest")) continue;
    if (excluded(nm, excl, nexcl)) continue;
    int replaced = 0;
    for (int k = 0; k < nfiles && !replaced; k++)
      if (gg_streq(zipnames[k], nm)) replaced = 1; /* the new copy wins */
    if (replaced) continue;
    reemit_cd(&cd, &g_rec[i]);
    /* keep a previous bundling in the manifest so it stays visible */
    if (g_have_manifest) {
      for (int u = 0; u < g_user_n; u++)
        if (gg_streq(g_user[u], nm)) manifest_add(&manifest, nm);
    }
    count++;
  }
  for (int i = 0; i < nfiles; i++) {
    size_t len = 0;
    char *src = gg_read_file(diskpaths[i], &len);
    if (!src) {
      snprintf(err, errsz, "%s: %s", diskpaths[i], strerror(errno));
      sb_free(&out_buf);
      sb_free(&cd);
      return -1;
    }
    add_stored(&out_buf, zipnames[i], src, len, &count, &cd);
    manifest_add(&manifest, zipnames[i]);
    free(src);
  }
  add_stored(&out_buf, ".gg-manifest", manifest.p, manifest.n, &count, &cd);
  sb_free(&manifest);
  unsigned int cd_off = (unsigned int)out_buf.n;
  sb_addn(&out_buf, cd.p, cd.n);
  unsigned int cd_size = (unsigned int)cd.n;
  sb_addf(&out_buf, "PK%c%c", 5, 6);
  wr16(&out_buf, 0); /* disk */
  wr16(&out_buf, 0); /* cd disk */
  wr16(&out_buf, count);
  wr16(&out_buf, count);
  wr32(&out_buf, cd_size);
  wr32(&out_buf, cd_off);
  wr16(&out_buf, 0); /* comment */
  int rc = gg_write_file(out, out_buf.p, out_buf.n);
  int saved = errno;
  sb_free(&out_buf);
  sb_free(&cd);
  if (rc != 0) {
    snprintf(err, errsz, "%s: %s", out, strerror(saved));
    return -1;
  }
  gg_chmod_x(out);
  return 0;
}

int bundle_expand_args(const char *const *args, int nargs, char ***zipnames_out,
                       char ***diskpaths_out, char *err, size_t errsz) {
  bundle_files bf;
  memset(&bf, 0, sizeof(bf));
  for (int i = 0; i < nargs; i++) {
    const char *arg = args[i];
    if (gg_is_dir(arg)) {
      /* "assets/" -> prefix "assets", so files land in assets/<name> */
      char *clean = gg_strdup(arg);
      size_t cl = strlen(clean);
      while (cl > 1 && clean[cl - 1] == '/') clean[--cl] = 0;
      const char *base = gg_basename(clean);
      bf_scan_dir(&bf, arg, *base ? base : ".");
      free(clean);
      continue;
    }
    if (!gg_is_file(arg)) {
      snprintf(err, errsz, "%s: %s", arg, gg_tr("no such file", "文件不存在"));
      bf_free(&bf);
      return -1;
    }
    /* name=path lets you pick the zip name explicitly */
    const char *eq = strchr(arg, '=');
    char *zipname = 0;
    const char *file = arg;
    if (eq && eq != arg) {
      zipname = gg_strndup(arg, (size_t)(eq - arg));
      file = eq + 1;
      if (!gg_is_file(file)) {
        snprintf(err, errsz, "%s: %s", file, gg_tr("no such file", "文件不存在"));
        free(zipname);
        bf_free(&bf);
        return -1;
      }
    } else if (gg_endswith(arg, ".lua") && !gg_streq(gg_basename(arg), "init.lua") &&
               !gg_streq(gg_basename(arg), "main.lua")) {
      const char *base = gg_basename(arg);
      zipname = gg_asprintf("modules/%s", base);
    } else {
      zipname = gg_strdup(gg_basename(arg));
    }
    bf_add(&bf, zipname, file);
    free(zipname);
  }
  *zipnames_out = bf.zipname;
  *diskpaths_out = bf.diskpath;
  return bf.n;
}

/* list the bundled modules (paths are "modules/<name>.lua") */
/* the entries gg added (from the manifest); i is 0-based */
int bundle_user_count(void) {
  bundle_scan();
  return g_user_n > 0 ? g_user_n : 0;
}

int bundle_has_manifest(void) {
  bundle_scan();
  return g_have_manifest;
}

const char *bundle_user_name(int i) {
  bundle_scan();
  if (g_user_n <= 0 || i < 0 || i >= g_user_n) return 0;
  return g_user[i];
}

int bundle_module_names(char ***names_out) {
  bundle_scan();
  char **names = 0;
  int n = 0;
  for (int i = 0; i < g_n; i++) {
    const char *nm = g_rec[i].name;
    if (!gg_startswith(nm, "modules/") || !gg_endswith(nm, ".lua")) continue;
    const char *base = nm + 8;
    size_t len = strlen(base) - 4;
    if (memchr(base, '/', len)) continue; /* one level only */
    names = realloc(names, (size_t)(n + 1) * sizeof(char *));
    names[n++] = gg_strndup(base, len);
  }
  *names_out = names;
  return n;
}
