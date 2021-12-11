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

#define _XOPEN_SOURCE 700
#include "csv.h"
#include <stdlib.h>
#ifdef __ARM_NEON__
#include <arm_neon.h>

typedef int32x4_t __m128i;
typedef char __v16qi __attribute__ ((vector_size (16)));

typedef union __attribute__((aligned(16))) __oword {
	int32x4_t m128i;
	uint8_t   u8[16];
} __oword;

static inline __m128i _mm_loadu_si128(const __m128i *p)
{
	__oword w;
	if (((intptr_t)p) & 0xf) {
		memcpy(&w.m128i, p, sizeof(*p));
		p = &w.m128i;
	}
	return vld1q_s32((int32_t *)p);
}


static inline uint16_t SSE4_cmpestrm(int32x4_t S1, int L1, int32x4_t S2, int L2)
{
    __oword s1, s2;
    s1.m128i = S1;
    s2.m128i = S2;
    uint16_t result = 0;
    uint16_t i = 0;
    uint16_t j = 0;
    for (i = 0; i < L2; i++) {
        for (j = 0; j < L1; j++) {
            if (s1.u8[j] == s2.u8[i]) {
                result |= (1 << i);
            }
        }
    }
    return result;
}


#else
#include <x86intrin.h>
#endif
#include <assert.h>
#include <string.h>
#include <stdio.h>

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)


typedef struct scan_t scan_t;
struct scan_t {
	uint16_t    bmap;
	const char* base;
	const char* q;
	__m128i     match;			/* qte, esc, delim, \r, \n */
	int         matchlen;
};


struct csv_parse_t {
	int	   fldmax;				/* num allocated elements in fld[] */
	int	   fldtop;				/* num used elements in fld[]. fld[fldtop-1] is valid */
	char** fld;					/* fld[] - points to each field */
	char** escptr;				/* escptr[i] points into a quoted fld[i], to the first esc char */
	int*   len;					/* len[] - length of each field */
	char   qte, esc, delim;		/* quote, escape, delim chars */
	char   nullstr[20];		    /* null indicator string */
	int	   nullstrsz;			/* strlen(nullstr) */

	char* lastbuf;				/* used by feed_last when we must add \n to end */

	struct {
		int64_t linenum;
		int64_t charnum;
		int64_t rownum;
		int64_t fldnum;

		int     errnum;
		const char* errmsg;
		int64_t elinenum;
		int64_t echarnum;
		int64_t erownum;
		int64_t efldnum;
	} state;

	/* SIMD vector for parsing quoted field */
	__m128i scan_escaped_string; /* qte, esc */

	scan_t scan;
};


static inline uint16_t fillbmap(const char* const p, const int plen, const __m128i match, const int matchlen)
{
	__m128i pval = _mm_loadu_si128((const __m128i*) p);
#ifdef __ARM_NEON__
	return SSE4_cmpestrm(match, matchlen, pval, plen);
#else
	__m128i mask = _mm_cmpestrm(match, matchlen, pval, plen, _SIDD_CMP_EQUAL_ANY | _SIDD_UBYTE_OPS);
	return _mm_extract_epi16(mask, 0);
#endif
}



/* setup the scan_t to scan p .. q */
static void scan_reset(scan_t* sp, const char* p, const char* q)
{
	sp->base = p;
	sp->q = q;
	sp->bmap = fillbmap(p, q-p, sp->match, sp->matchlen);
}

static int __scan_forward(scan_t* sp)
{
	const char* base = sp->base;
	const char* const q = sp->q;
	char tmpbuf[16];
	while (0 == sp->bmap) {
		base += 16;
		const char* p = base;
		const int plen = q - base;
		if (unlikely(plen < 16)) {
			if (unlikely(plen <= 0)) return -1;
			// We will load 16-byte in fillbmap. If there is
			// less than 16-byte in base, copy into tmpbuf
			// and read from tmpbuf.
			memcpy(tmpbuf, p, plen);
			p = tmpbuf;
		}
		sp->bmap = fillbmap(p, plen, sp->match, sp->matchlen);
	}
	sp->base = base;
	return 0;
}


/* this is the main workhorse. return ptr to the next special char */
static inline const char* scan_next(scan_t* sp)
{
	if (0 == sp->bmap) {
		if (__scan_forward(sp))
			return 0;
	}
	int off = __builtin_ffs(sp->bmap) - 1;
	sp->bmap &= ~(1 << off);
	return sp->base + off;
}


/* there are more fields than the current cp->fld[]. expand it. */
static int expand(csv_parse_t* cp)
{
	void* xp;
	int max = cp->fldmax + 64;

	if (! (xp = realloc(cp->fld, sizeof(*cp->fld) * max))) {
		return -1;
	}
	cp->fld = xp;

	if (! (xp = realloc(cp->escptr, sizeof(*cp->escptr) * max))) {
		return -1;
	}
	cp->escptr = xp;

	if (! (xp = realloc(cp->len, sizeof(*cp->len) * max))) {
		return -1;
	}
	cp->len = xp;

	cp->fldmax = max;
	return 0;
}



/* save error state and return errnum */
static int reterr(csv_parse_t* cp,
				  int errnum,
				  const char* const errmsg,
				  int cno,
				  int nline,
				  int nchar)
{
	cp->state.errnum = errnum;
	cp->state.errmsg = errmsg;
	cp->state.elinenum = cp->state.linenum + nline;
	cp->state.echarnum = cp->state.charnum + nchar;
	cp->state.erownum = cp->state.rownum + 1;
	cp->state.efldnum = cno;
	return -1;
}

/* helper to free a malloc */
static void xfree(const void* s)
{
	if (s) free( (void*) s);
}


/**
 *	touchup - NUL terminate, replace nullstr, and unescape each field
 */
static void touchup(csv_parse_t* cp)
{
	const char* const nullstr = cp->nullstr;
	const int nullstrsz = cp->nullstrsz;
	const char esc = cp->esc;
	const char qte = cp->qte;
	(void) esc; (void) qte; /* prevent unused var warning */

	/* process the fields one by one */
	for (int i = 0; i < cp->fldtop; i++) {
		char** const fld = &cp->fld[i];
		char* p = *fld;
		char* q = p + cp->len[i];

		*q = 0; /* NUL term */

		char* const escptr = cp->escptr[i];
		if (!escptr) {
			// simple case: no escape chars.
			// check for null
			if (q - p == nullstrsz && 0 == memcmp(p, nullstr, q - p)) {
				*fld = 0;		/* make it a nullptr to indicate sql NULL field */
			}
			// done with this field
			continue;
		}

		/*
		 * IN A QUOTED FIELD. Squeeze out the escape chars.
		 */

		// start from escptr
		assert(p <= escptr && escptr < q);
		char* s = p = escptr;
		assert(*p == esc);

		p++;					/* skip the first esc char */
		*s++ = *p++;			/* copy the escaped char */

		// scan forward and squeeze out subsequent esc chars, if any
		while (p < q) {
			if (*p == qte || *p == esc) {
				p++;			/* skip the special char */
			}

			*s++ = *p++;		/* copy the escaped char */
		}

		*s = 0;				/* NUL term */
	}
}



int csv_line(csv_parse_t* const cp, const char* buf, int bufsz)
{
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
	(void) qte, (void) esc;		/* prevent gcc unused-var warning */

	const char delim = cp->delim;
	const char* ppp = buf;
	const char* const q = ppp + bufsz;


	int	   cno = 0;				/* start at field 0 */
	int	   nline = 0;			/* count num lines */

	const char** fld;					/* points at cp->fld[cno] */
	int quoted;
	const char* escptr;
	scan_t* scan = &cp->scan;
	scan_reset(scan, ppp, q);

	START_VAL: {
		if (unlikely(cno >= cp->fldmax)) {
			if (expand(cp)) {
				return reterr(cp, CSV_EOUTOFMEMORY, "out of memory",
							  cno, nline, ppp - buf);
			}
			assert(cno < cp->fldmax);
		}

		/* field starts here */
		fld = (const char**) &cp->fld[cno];
		*fld = ppp;
		escptr = 0;

		// point ppp at next special char
		if (0 == (ppp = scan_next(scan)))
			return 0;

		// quoted when ppp is at the first char and it is a qte
		quoted = (ppp == *fld && *ppp == qte);

		// a value can be either quoted or unquoted
		if (unlikely(quoted)) {
			goto QUOTED_VAL;
		} else {
			// We are either inside an UNQUOTED VAL or at end of a field
			// ch could be qte, esc, delim, \r or \n
			int ch = *ppp;
			if (unlikely(ch == qte || ch == esc)) {
				// qte or esc is not permitted in an unquoted field
				return reterr(cp, CSV_EQUOTE, "unexpected qte or esc char in unquoted field",
							  cno, nline, ppp - buf);
			}
			// ppp is at a delim or CR or LF
			goto FINISH;
		}
	}

	QUOTED_VAL: {
		if (0 == (ppp = scan_next(scan)))		/* next special char */
			return 0;

		const int cur = *ppp;
		if (cur == qte) {
			const char* xp;
			if (0 == (xp = scan_next(scan)))
				return 0;

			// expect a special char after qte
			if (unlikely(xp != ppp + 1)) {
				return reterr(cp, CSV_EQUOTE, "bad value after quote", cno, nline, ppp - buf);
			}
			// handle qte qte
			if (esc == qte) {
				if (*xp == qte) {
					escptr = escptr ? escptr : ppp;
					goto QUOTED_VAL;
				}
			}
			ppp = xp;
			goto FINISH;
		}

		if (cur == esc) {
			assert( esc != qte);
			escptr = escptr ? escptr : ppp;
			ppp += 2;				/* skip 2 chars */
			if (ppp >= q)
				return 0;

			// reset the scan to start at ppp
			scan_reset(scan, ppp, q);
			goto QUOTED_VAL;
		}

		// ignore these values inside a quote
		assert(cur == '\n' || '\r' || cur == delim);
		nline += (cur == '\n') ? 1 : 0; /* line accounting */
		goto QUOTED_VAL;				/* still in same field */
	}

	FINISH: {
		/* ppp is pointing at [delim, \r, \n] */
		assert(*ppp == delim || *ppp == '\r' || *ppp == '\n');

		/* fin the field */
		cp->len[cno] = ppp - *fld;
		cp->escptr[cno] = (char*) escptr;

		if (quoted) {
			/* point between the first and last quote char */
			cp->fld[cno] += 1;
			cp->len[cno] -= 2;
		}

		cno++;

		// eat the cur char
		const int cur = *ppp++;

		/* the field is done? */
		if (likely(cur == delim)) {
			goto START_VAL;
		}

		/* the row is done! */

		cp->fldtop = cno;
		if (cur == '\n' || (cur == '\r' && ppp < q && *ppp == '\n' && ppp++)) {
			goto FINROW;
		}
		return reterr(cp, CSV_ECRLF, "CRLF expected",
					  cno, nline, ppp - buf);
	}


	FINROW: {
		int rowsz = ppp - buf;
		nline++;
		cp->state.linenum += nline;
		cp->state.rownum++;
		cp->state.charnum += rowsz;

		return rowsz;
	}
}



int csv_feed(csv_parse_t* const cp,
			 char*   buf,
			 int	 bufsz,
			 char*** ret_field,
			 int*	 ret_nfield)
{
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


int csv_feed_last(csv_parse_t* const cp,
				  char*   buf,
				  int	  bufsz,
				  char*** ret_field,
				  int*	  ret_nfield)
{
	*ret_field = 0;
	*ret_nfield = 0;

	if (bufsz <= 0)
		return bufsz == 0 ? 0 : reterr(cp, CSV_EPARAM, "bad bufsz", 0, 0, 0);

	/* handle the case where last row is missing \n */
	int appended = 0;
	if (buf[bufsz-1] != '\n') {
		xfree(cp->lastbuf);
		cp->lastbuf = malloc(bufsz + 2);
		if (! cp->lastbuf) {
			return reterr(cp, CSV_EOUTOFMEMORY, "out of memory", 0, 0, 0);
		}
		memcpy(cp->lastbuf, buf, bufsz);
		cp->lastbuf[bufsz] = '\n';
		cp->lastbuf[bufsz+1] = '\0';
		buf = cp->lastbuf;
		bufsz++;
		appended = 1;
	}

	int n = csv_feed(cp, buf, bufsz, ret_field, ret_nfield);
	return (n > 0 && appended) ? n - 1 : n;
}


csv_parse_t* csv_open(int qte,
					  int esc,
					  int delim,
					  const char nullstr[20])
{
	/* default values */
	qte = qte ? qte : '"';
	esc = esc ? esc : qte;
	delim = delim ? delim : ',';
	nullstr = nullstr ? nullstr : "";

	/* alloc parse struct and init it */
	csv_parse_t* cp;
	if (! (cp = calloc(1, sizeof(csv_parse_t)))) {
		return 0;
	}
	strncpy(cp->nullstr, nullstr, sizeof(cp->nullstr));
	cp->nullstr[sizeof(cp->nullstr)-1] = 0;
	cp->nullstrsz = strlen(cp->nullstr);

	cp->qte = qte;
	cp->esc = esc;
	cp->delim = delim;

	/* match while inside a quoted string field */
	__v16qi v2 = { qte, esc };
	cp->scan_escaped_string = (__m128i) v2;

	/* match while outside a quoted string field */
	__v16qi v5 = { qte, esc, delim, '\r', '\n' };
	cp->scan.match = (__m128i) v5;
	cp->scan.matchlen = 5;

	return cp;
}



void csv_close(csv_parse_t* cp)
{
	if (cp) {
		xfree(cp->fld);
		xfree(cp->escptr);
		xfree(cp->len);
		xfree(cp->lastbuf);
		xfree(cp);
	}
}


int csv_errnum(csv_parse_t* cp) { return cp->state.errnum; }
const char* csv_errmsg(csv_parse_t* cp) { return cp->state.errmsg; }
int csv_errlinenum(csv_parse_t* cp) { return cp->state.elinenum; }
int csv_errcharnum(csv_parse_t* cp) { return cp->state.echarnum; }
int csv_errrownum(csv_parse_t* cp) { return cp->state.erownum; }
int csv_errfldnum(csv_parse_t* cp) { return cp->state.efldnum; }



int csv_scan(intptr_t handle,
			 int qte,
			 int esc,
			 int delim,
			 const char nullstr[20],
			 int (*on_bufempty)(intptr_t handle, char* buf, int bufsz),
			 int (*on_row)(intptr_t handle, int64_t rownum, char** field, int nfield),
			 void (*on_error)(intptr_t handle,
							  int errtype,
							  const char* errmsg,
							  csv_parse_t* cp))
{
	int   bufsz = 1024 * 1024;
	char* buf = 0;
	char* p = buf;
	char* q = buf;
	int   eof = 0;
	csv_parse_t* cp = 0;
	int nb;
	int nfield;
	char** field;
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
			memmove(buf, p, q-p);
			q = buf + (q-p);
			p = buf;
		}
		assert(p == buf);

		// expand buf[] if p..q fills up the whole buf
		if (q - p == bufsz) {
			char* newbuf;
			int   newsz = bufsz + 1024 * 1024 * 10;

			if (! (newbuf = realloc(buf, newsz))) {
				sprintf(msg, "cannot expand buffer beyond %d bytes", bufsz);
				on_error(handle, CSV_EOUTOFMEMORY, msg, 0);
				goto bail;
			}
			buf = newbuf;
			bufsz = newsz;
			q = buf + (q-p);
			p = buf;
		}

		// fill
		assert(!eof);
		nb = on_bufempty(handle, q, bufsz - (q-p));
		if (nb < 0)
			goto bail;

		eof |= (nb == 0);
		q   += nb;

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
