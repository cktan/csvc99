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
#ifndef CSV_H
#define CSV_H

/*
 
  This is a very simple csv parser that uses SSE instructions for speed. On 
  core i7-7500U, scan speed exceeds 840MB/s. 

  General usage:

     csv_open()
	 csv_feed()
	 csv_feed()
	 ...
	 csv_feed_last()
	 csv_close()

*/

#include <stdint.h>

#ifdef __cplusplus
#define CSV_EXTERN extern "C"
#else
#define CSV_EXTERN extern
#endif

#define CSV_EINTERNAL    -100	/* internal error */
#define CSV_EPARAM       -101   /* param error */
#define CSV_ECRLF        -102   /* problem with CRLF */
#define CSV_EQUOTE       -103   /* escape or quote error */
#define CSV_EOUTOFMEMORY -104   /* OOM */
#define CSV_EROWTOOLONG  -105	/* for csv_scan, buffer overflow */
#define CSV_EEXTRAINPUT  -106	/* for csv_scan, parse error  */

typedef struct csv_parse_t csv_parse_t;


/**
 * Create a parser. Returns NULL on out-of-memory error.
 *
 * Nullstr must not be longer than 20 chars inclusive of the NUL terminator, 
 * and should not contain special chars like quotes, escapes, delims, CR, LF.
 *
 * For each param that is 0 (or NULL), it assumes its default value.
 *
 * For qte, the default value is the double-quote char. 
 * For esc, the default value is the value of qte.
 * For delim, the default value is the comma char.
 * For nullstr, the default value is an empty string.
 * 
 */
CSV_EXTERN csv_parse_t* csv_open(int qte,	/* quote char */
								 int esc,	/* escape char */
								 int delim,	/* delim char */
								 const char nullstr[20]);	/* sql NULL representation */
/**
 * Destroy the parser
 */
CSV_EXTERN void csv_close(csv_parse_t* cp);



/**
 * Parse the next row.
 * Returns
 *    a) a postive integer indicating #bytes consumed in buf,
 *    b) 0 if buf does not have a complete row, or
 *    c) -1 on error.
 *
 * Also returns the fields in the row in field[], and the size of field[]. 
 * The data in the fields have been unescaped and unquoted. 
 *
 * NULL fields are indicated by null pointers in field[].
 *
 */
CSV_EXTERN int csv_feed(csv_parse_t* const cp,
						char*   buf,
						int     bufsz,
						char*** ret_field,
						int*    ret_nfield);

/**
 * Parse the last row. Same functionality as csv_parse_next but 
 * also handles optional newline for last row. See csv_feed
 * for details on params and returns.
 *
 * If the returned #bytes consumed in buf is not equal to bufsz, then there
 * are extra bytes at the end of buffer.
 */
CSV_EXTERN int csv_feed_last(csv_parse_t* const cp,
							 char*   buf,
							 int     bufsz,
							 char*** ret_field,
							 int*    ret_nfield);


/**
 * Get error info.
 */
CSV_EXTERN int csv_errnum(csv_parse_t* cp);
CSV_EXTERN const char* csv_errmsg(csv_parse_t* cp);
CSV_EXTERN int csv_errlinenum(csv_parse_t* cp);
CSV_EXTERN int csv_errcharnum(csv_parse_t* cp);
CSV_EXTERN int csv_errrownum(csv_parse_t* cp);
CSV_EXTERN int csv_errfldnum(csv_parse_t* cp);



/**
 * Parse the next row quickly while disregarding fields. Great
 * for locating row boundaries or counting rows without processing
 * the data.
 */
CSV_EXTERN int csv_line(csv_parse_t* const cp, const char* buf, int bufsz);



/**
 *  Scan using callbacks. Maximum row size is fixed at 10MB.
 *
 *  on_bufempty: callback to fill buffer with data; return #bytes 
 *               written to buf[]; 0 if no more data; -1 on error.
 *
 *  on_row: callback to process fields read out of buf. return 0
 *          on success; -1 on error.
 *
 *  on_error: callback to report errors during scan. For non parser 
 *            related errors, cp will be NULL, and errtype/errmsg
 *            shall indicate the error. For parser related errors,
 *            cp will be a non-NULL that can be used to interrogate 
 *            for error messages with csv_errnum(cp), csv_errmsg(cp).
 */
CSV_EXTERN int csv_scan(intptr_t handle,
						int qte,
						int esc,
						int delim,
						const char nullstr[20],
						int (*on_bufempty)(intptr_t handle, char* buf, int bufsz),
						int (*on_row)(intptr_t handle,
									  int64_t rownum,
									  char** field,
									  int nfield),
						void (*on_error)(intptr_t handle,
										 int errtype,
										 const char* errmsg,
										 csv_parse_t* cp));

#endif /*CSV_H*/
