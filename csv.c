/*
  CSVC99 - SIMD-accelerated csv parser in C99
  Copyright (c) 2019-2020 CK Tan
  cktanx@gmail.com

  CSVC99 can be used for free under the GNU General Public License
  version 3, where anything released into public must be open source,
  or under a commercial license. The commercial license does not
  cover derived or ported versions created by third parties under
  GPL. To inquire about commercial license, please send email to
  cktanx@gmail.com.
*/

#include "csv.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <x86intrin.h>

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

typedef struct scan_t scan_t;
struct scan_t {
  uint32_t bmap;
  const char *base;
  const char *q;
  char qte;
  char esc;
  char delim;
};

struct csv_parse_t {
  int fldmax;           /* num allocated elements in fld[] */
  int fldtop;           /* num used elements in fld[]. fld[fldtop-1] is valid */
  char **fld;           /* fld[] - points to each field */
  int *len;             /* len[] - length of each field */
  char *quoted;         /* quoted[] - flag if field is quoted */
  char qte, esc, delim; /* quote, escape, delim chars */
  char nullstr[20];     /* null indicator string */
  int nullstrsz;        /* strlen(nullstr) */

  char *lastbuf; /* used by feed_last when we must add \n to end */

  struct {
    int64_t linenum;
    int64_t charnum;
    int64_t rownum;
    int64_t fldnum;

    int errnum;
    const char *errmsg;
    int64_t elinenum;
    int64_t echarnum;
    int64_t erownum;
    int64_t efldnum;
  } state;

  scan_t scan;
};

static inline uint32_t fillbmap(const __m256i *p, char qte, char esc,
                                char delim) {
  __m256i src = _mm256_loadu_si256(p);
  __m256i pat0 = _mm256_set1_epi8(qte);
  __m256i pat1 = _mm256_set1_epi8(esc);
  __m256i pat2 = _mm256_set1_epi8(delim);
  __m256i pat3 = _mm256_set1_epi8('\n');

  int32_t flag0 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(src, pat0));
  int32_t flag1 =
      (esc == qte) ? 0 : _mm256_movemask_epi8(_mm256_cmpeq_epi8(src, pat1));
  int32_t flag2 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(src, pat2));
  int32_t flag3 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(src, pat3));

  // return ntohl(flag0|flag1|flag2|flag3);
  return flag0 | flag1 | flag2 | flag3;
}

/* setup the scan_t to scan p .. q */
static void scan_reset(scan_t *sp, const char *p, const char *q, char qte,
                       char esc, char delim) {
  char tmpbuf[32];
  sp->base = p;
  sp->q = q;
  sp->qte = qte;
  sp->esc = esc;
  sp->delim = delim;
  int len = q - p;
  if (len < 32) {
    memcpy(tmpbuf, p, len);
    p = tmpbuf;
  }
  sp->bmap = fillbmap((const __m256i *)p, sp->qte, sp->esc, sp->delim);
}

static int __scan_forward(scan_t *sp) {
  const char *base = sp->base;
  char tmpbuf[32];
  const char *q = sp->q;
  while (0 == sp->bmap) {
    base += 32;
    const char *p = base;
    const int plen = q - base;
    if (unlikely(plen < 32)) {
      if (plen < 0) {
        return -1;
      }
      // We will load 32-byte in fillbmap. If there is
      // less than 32-byte in base, copy into tmpbuf
      // and read from tmpbuf.
      memcpy(tmpbuf, p, plen);
      p = tmpbuf;
    }
    sp->bmap = fillbmap((const __m256i *)p, sp->qte, sp->esc, sp->delim);
  }
  sp->base = base;
  return 0;
}

/* this is the main workhorse. return ptr to the next special char */
static inline __attribute__((always_inline)) const char *scan_next(scan_t *sp) {
  const char *ret = 0;
  if (0 == sp->bmap && __scan_forward(sp)) {
    ;
  } else {
    int off = __builtin_ffs(sp->bmap) - 1;
    sp->bmap &= ~(1 << off);
    ret = sp->base + off;
    ret = ret < sp->q ? ret : 0;
  }
  return ret;
}

/* there are more fields than the current cp->fld[]. expand it. */
static int expand(csv_parse_t *cp) {
  void *xp;
  int max = cp->fldmax * 1.2 + 64;

  if (!(xp = realloc(cp->fld, sizeof(*cp->fld) * max))) {
    return -1;
  }
  cp->fld = xp;

  if (!(xp = realloc(cp->len, sizeof(*cp->len) * max))) {
    return -1;
  }
  cp->len = xp;

  if (!(xp = realloc(cp->quoted, sizeof(*cp->quoted) * max))) {
    return -1;
  }
  cp->quoted = xp;

  cp->fldmax = max;
  return 0;
}

/* save error state and return errnum */
static int reterr(csv_parse_t *cp, int errnum, const char *const errmsg,
                  int cno, int nline, int nchar) {
  cp->state.errnum = errnum;
  cp->state.errmsg = errmsg;
  cp->state.elinenum = cp->state.linenum + nline;
  cp->state.echarnum = cp->state.charnum + nchar;
  cp->state.erownum = cp->state.rownum + 1;
  cp->state.efldnum = cno;
  return -1;
}

/**
 *	touchup - NUL terminate, replace nullstr, and unescape each field
 */
static void touchup(csv_parse_t *cp) {
  const char *const nullstr = cp->nullstr;
  const int nullstrsz = cp->nullstrsz;
  const char esc = cp->esc;
  const char qte = cp->qte;
  (void)esc;
  (void)qte; /* prevent unused var warning */

  /* process the fields one by one */
  const int top = cp->fldtop;
  for (int i = 0; i < top; i++) {
    char **const fld = &cp->fld[i];
    char *p = *fld;
    char *q = p + cp->len[i];

    *q = 0; /* NUL term */
    if (q - p == nullstrsz && 0 == memcmp(p, nullstr, nullstrsz)) {
      *fld = 0; /* make it a nullptr to indicate sql NULL field */
      continue;
    }

    if (!cp->quoted[i]) {
      continue;
    }

    int inquote = 0;
    char *start = p;
    char *s = p;
    while (p < q) {
      char ch = *p++;
      int special = (ch == esc) | (ch == qte);
      if (unlikely(special)) {
        if (inquote && ch == esc) {
          char nextch = (p < q ? *p : 0);
          if (nextch == qte || nextch == esc) {
            // do the escape
            p++;
            *s++ = nextch;
            continue;
          }
          // ignore the escape
        }
        if (ch == qte) {
          inquote = !inquote;
          continue;
        }
        // fallthru
      }
      *s++ = ch;
    }
    assert(!inquote);
    *s = 0; /* NUL term */

    cp->fld[i] = start;
    cp->len[i] = s - start;
  }

  // remove the last \r in the last field
  if (top > 0) {
    char *p = cp->fld[top - 1];
    char *q = p + cp->len[top - 1];
    if (q - p > 0 && q[-1] == '\r') {
      *--q = 0;
      cp->len[top - 1] = q - p;
      if (q - p == nullstrsz && 0 == memcmp(p, nullstr, nullstrsz)) {
        cp->fld[top - 1] = 0; /* make it a nullptr to indicate sql NULL field */
      }
    }
  }
}

int csv_line(csv_parse_t *const cp, const char *buf, int bufsz) {
  /*
   * NOTE: this routine MUST NOT modify buf[]; it should only index
   * fld[] into buf[].  When it succeeded, then buf[] can be modified
   * later via a call to touchup().
   */
  if (unlikely(!buf || bufsz <= 0)) {
    return bufsz == 0 ? 0 : reterr(cp, CSV_EPARAM, "bad bufsz", 0, 0, 0);
  }
  const char qte = cp->qte;
  const char esc = cp->esc;
  (void)qte, (void)esc; /* prevent gcc unused-var warning */

  const char delim = cp->delim;
  const char *ppp = buf;
  const char *const q = ppp + bufsz;

  int cno = 0;   /* start at field 0 */
  int nline = 0; /* count num lines */

  const char **fld; /* points at cp->fld[cno] */
  scan_t *scan = &cp->scan;
  scan_reset(scan, ppp, q, qte, esc, delim);
  int quoted = 0;

STARTVAL : {
  if (unlikely(cno >= cp->fldmax)) {
    if (expand(cp)) {
      return reterr(cp, CSV_EOUTOFMEMORY, "out of memory", cno, nline,
                    ppp - buf);
    }
    assert(cno < cp->fldmax);
  }

  /* field starts here */
  fld = (const char **)&cp->fld[cno];
  *fld = ppp;
  goto UNQUOTED;
}

UNQUOTED : {
  // point ppp at next special char
  if (0 == (ppp = scan_next(scan)))
    return 0;

  const char ch = *ppp;
  if (likely(ch == delim || ch == '\n'))
    goto ENDVAL;

  if (ch == qte) {
    goto QUOTED;
  }

  assert(ch == esc); /* ignore */
  goto UNQUOTED;     /* still in UNQUOTED */
}

QUOTED : {

  quoted = 1;
  if (0 == (ppp = scan_next(scan)))
    return 0;

  const char ch = *ppp;
  if (ch == esc) {
    char nextch = (ppp + 1 < q ? ppp[1] : 0);
    if (nextch == qte || nextch == esc) {
      if (unlikely(ppp + 1 != scan_next(scan))) {
        return reterr(cp, CSV_EINTERNAL, "internal error: bad pointer value",
                      cno, nline, ppp - buf);
      }
      goto QUOTED;
    }
    if (nextch == 0) {
      return 0;
    }
    // fallthru
  }
  if (likely(ch == qte)) {
    // close quote
    goto UNQUOTED;
  }

  goto QUOTED;
}

ENDVAL : {
  /* ppp is pointing at [delim, \n] */
  assert(*ppp == delim || *ppp == '\n');

  /* fin the field */
  cp->len[cno] = ppp - *fld;
  cp->quoted[cno] = quoted;
  cno++;

  const char ch = *ppp++;

  if (likely(ch == delim)) {
    goto STARTVAL; /* start next field */
  }

  /* the field is done? */
  cp->fldtop = cno;
  goto FINROW;
}

FINROW : {
  int rowsz = ppp - buf;
  nline++;
  cp->state.linenum += nline;
  cp->state.rownum++;
  cp->state.charnum += rowsz;

  return rowsz;
}
}

int csv_feed(csv_parse_t *const cp, char *buf, int bufsz, char ***ret_field,
             int *ret_nfield) {
  *ret_field = 0;
  *ret_nfield = 0;

  int rowsz = csv_line(cp, buf, bufsz);
  if (rowsz <= 0) {
    // insufficient chars in buf for a row
    return rowsz;
  }

  // we have a row!
  *ret_field = cp->fld;
  *ret_nfield = cp->fldtop;

  // go back to fix up fields with escaped chars
  touchup(cp);

  return rowsz;
}

int csv_feed_last(csv_parse_t *const cp, char *buf, int bufsz,
                  char ***ret_field, int *ret_nfield) {
  *ret_field = 0;
  *ret_nfield = 0;

  if (bufsz <= 0)
    return bufsz == 0 ? 0 : reterr(cp, CSV_EPARAM, "bad bufsz", 0, 0, 0);

  /* handle the case where last row is missing \n */
  int appended = 0;
  if (buf[bufsz - 1] != '\n') {
    free(cp->lastbuf);
    cp->lastbuf = malloc(bufsz + 2);
    if (!cp->lastbuf) {
      return reterr(cp, CSV_EOUTOFMEMORY, "out of memory", 0, 0, 0);
    }
    memcpy(cp->lastbuf, buf, bufsz);
    cp->lastbuf[bufsz] = '\n';
    cp->lastbuf[bufsz + 1] = '\0';
    buf = cp->lastbuf;
    bufsz++;
    appended = 1;
  }

  int n = csv_feed(cp, buf, bufsz, ret_field, ret_nfield);
  return (n > 0 && appended) ? n - 1 : n;
}

csv_parse_t *csv_open(int qte, int esc, int delim, const char nullstr[20]) {
  /* default values */
  qte = qte ? qte : '"';
  esc = esc ? esc : qte;
  delim = delim ? delim : ',';
  nullstr = nullstr ? nullstr : "";

  /* alloc parse struct and init it */
  csv_parse_t *cp;
  if (!(cp = calloc(1, sizeof(csv_parse_t)))) {
    return 0;
  }
  strncpy(cp->nullstr, nullstr, sizeof(cp->nullstr));
  cp->nullstr[sizeof(cp->nullstr) - 1] = 0;
  cp->nullstrsz = strlen(cp->nullstr);

  cp->qte = qte;
  cp->esc = esc;
  cp->delim = delim;

  return cp;
}

void csv_close(csv_parse_t *cp) {
  if (cp) {
    free(cp->fld);
    free(cp->len);
    free(cp->quoted);
    free(cp->lastbuf);
    free(cp);
  }
}

int csv_errnum(csv_parse_t *cp) { return cp->state.errnum; }
const char *csv_errmsg(csv_parse_t *cp) { return cp->state.errmsg; }
int csv_errlinenum(csv_parse_t *cp) { return cp->state.elinenum; }
int csv_errcharnum(csv_parse_t *cp) { return cp->state.echarnum; }
int csv_errrownum(csv_parse_t *cp) { return cp->state.erownum; }
int csv_errfldnum(csv_parse_t *cp) { return cp->state.efldnum; }

int csv_scan(intptr_t handle, int qte, int esc, int delim,
             const char nullstr[20],
             int (*on_bufempty)(intptr_t handle, char *buf, int bufsz),
             int (*on_row)(intptr_t handle, int64_t rownum, char **field,
                           int nfield),
             void (*on_error)(intptr_t handle, int errtype, const char *errmsg,
                              csv_parse_t *cp)) {
  int bufsz = 1024 * 1024;
  char *buf = 0;
  char *p = buf;
  char *q = buf;
  int eof = 0;
  csv_parse_t *cp = 0;
  int nb;
  int nfield;
  char **field;
  char msg[100];

  if (0 == (buf = malloc(bufsz))) {
    on_error(handle, CSV_EOUTOFMEMORY, "out of memory", 0);
    goto bail;
  }

  cp = csv_open(qte, esc, delim, nullstr);
  if (!cp) {
    on_error(handle, CSV_EOUTOFMEMORY, "csv_open failed", 0);
    goto bail;
  }

  // keep filling up buf[] and feeding csv until eof
  while (!eof) {
    // shift p..q to start of buf
    if (p != buf) {
      memmove(buf, p, q - p);
      q = buf + (q - p);
      p = buf;
    }
    assert(p == buf);

    // expand buf[] if p..q fills up the whole buf
    if (q - p == bufsz) {
      char *newbuf;
      int newsz = bufsz * 1.5;

      if (!(newbuf = realloc(buf, newsz))) {
        sprintf(msg, "cannot expand buffer beyond %d bytes", bufsz);
        on_error(handle, CSV_EOUTOFMEMORY, msg, 0);
        goto bail;
      }
      buf = newbuf;
      bufsz = newsz;
      q = buf + (q - p);
      p = buf;
    }

    // fill
    assert(!eof);
    nb = on_bufempty(handle, q, bufsz - (q - p));
    if (nb < 0)
      goto bail;

    eof |= (nb == 0);
    q += nb;

    // keep feeding until there is no more complete row in buf[]
    while (p < q) {
      nb = csv_feed(cp, p, q - p, &field, &nfield);
      if (unlikely(nb <= 0)) {
        if (nb == 0)
          break;
        else {
          on_error(handle, 0, 0, cp);
          goto bail;
        }
      }
      if (on_row(handle, cp->state.rownum, field, nfield)) {
        goto bail;
      }
      p += nb;
    }
  }

  // one last row might remain in buf[]
  if (p < q) {
    nb = csv_feed_last(cp, p, q - p, &field, &nfield);
    if (nb < 0) {
      on_error(handle, 0, 0, cp);
      goto bail;
    }
    if (on_row(handle, cp->state.rownum, field, nfield)) {
      goto bail;
    }
    p += nb;
  }

  if (p != q) {
    on_error(handle, CSV_EEXTRAINPUT, "extra data after last row", 0);
    goto bail;
  }

  free(buf);
  return 0;

bail:
  free(buf);
  return -1;
}
