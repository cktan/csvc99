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

const char* usagestr = "\n\
  USAGE: %s [-h] [-d delim] [-q quote] [-e esc] [-n nullstr] [FILE]\n\
                        \n\
                        \n\
  Print a csv file in a format that can be read into a \n\
  python program and eval() into a python object\n\
                                           \n\
  For example, a csv file with these lines:\n\
    1|2|(null)|3                           \n\
    \"abcd\"|efg|hij|klm                   \n\
                                           \n\
  will be printed as:                      \n\
                                           \n\
   [                                       \n\
     ['1','2',None,'3'],                   \n\
     ['abcd','efg','hij','klm']            \n\
   ]                                       \n\
                        \n\
  OPTIONS:              \n\
                        \n\
      -h         : print this message          \n\
      -q quote   : specify quote char; default to double-quote           \n\
      -e esc     : specify escape char; default to the quote char        \n\
      -n nullstr : specify string representing null; default to \"\"     \n\
      \n\
";

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include "csv.h"

const char* pname = 0;
const char* fname = 0;
int qte = '"';
int esc = '"';
int delim = ',';
char nullstr[20] = {0};


#define perr(M, ...) fprintf(stderr, M, ##__VA_ARGS__)
#define pout(M, ...) fprintf(stdout, M, ##__VA_ARGS__)
#define fatal(M, ...) do { fprintf(stderr, M, ##__VA_ARGS__); exit(1); } while (0)


void usage(int exitcode, const char* msg)
{
    perr(usagestr, pname);
    if (msg) {
        perr("\n%s\n", msg);
    }
    exit(exitcode);
}

void parse_cmdline(int argc, char* const* argv)
{
    pname = argv[0];
    int opt;
    char *q, *e, *d, *n;
    q = e = d = n = 0;
    while ((opt = getopt(argc, argv, "d:q:e:n:h")) != -1) {
        switch (opt) {
        case 'd': d = optarg; break;
        case 'q': q = optarg; break;
        case 'e': e = optarg; break;
        case 'n': n = optarg; break;
		case 'h': usage(0, 0); break;
        default: usage(1, 0); break;
        }
    }
    
    /* fname */
    if (optind == argc) 
        ; /* read from stdin */
    else if (optind + 1 == argc) 
        fname = argv[optind];
    else 
        usage(1, "Error: please supply only one filename");


    /* qte */
    if (q) {
        if (strlen(q) != 1) {
            usage(1, "Error: -q quote-char expects a single char.");
        }
        qte = q[0];
    }

    /* esc */
    if (e) {
        if (strlen(e) != 1) {
            usage(1, "Error: -e escape-char expects a single char.");
        }
        esc= e[0];
    }

    /* delim */
    if (d) {
        if (strlen(d) != 1) {
            usage(1, "Error: -d delim-char expects a single char.");
        }
        delim = d[0];
    }
	
	/* nullstr */
	if (n) {
		if (strlen(n) >= 20) {
			usage(1, "Error: -n nullstr is too long. max is 19 chars");
		}
		strcpy(nullstr, n);
	}
}


struct {
	int64_t nbytes;
	int64_t nrows;
	int     min_ncols, max_ncols;
	int     min_rowsz, max_rowsz;
} tot = {0};


int do_read(intptr_t handle, char* buf, int bufsz)
{
	FILE* fp = (FILE*) handle;
	int nb = fread(buf, 1, bufsz, fp);
	if (nb > 0)
		tot.nbytes += nb;
	return nb;
}


int do_row(intptr_t handle,
		   int64_t rownum,
		   char** col,
		   int ncol)
{
	(void) handle;
	(void) col;
	char* last = col[ncol-1];
	int rowsz = last + strlen(last) - col[0] + 1;
	
	tot.nrows = rownum;

	if (tot.min_ncols == 0 || ncol < tot.min_ncols)
		tot.min_ncols = ncol;
	if (tot.max_ncols < ncol)
		tot.max_ncols = ncol;

	if (tot.min_rowsz == 0 || rowsz < tot.min_rowsz)
		tot.min_rowsz = rowsz;
	if (tot.max_rowsz < rowsz)
		tot.max_rowsz = rowsz;

	return 0;
}



void do_error(intptr_t handle, int errtype, const char* errmsg, csv_parse_t* cp)
{
	(void) handle;
	(void) errtype;
	errmsg = cp ? csv_errmsg(cp) : errmsg;
	fatal("ERROR: %s\n", csv_errmsg(cp));
}


int main(int argc, char* argv[])
{
    parse_cmdline(argc, argv);
    FILE* fp = stdin;

	if (fname && ! (fp = fopen(fname, "r"))) {
		perr("ERROR: fopen %s - %s\n", fname, strerror(errno));
		exit(1);
    }

	csv_scan((intptr_t)fp,
			 qte, esc, delim, nullstr,
			 do_read,
			 do_row,
			 do_error);

    fclose(fp);


	printf("      #bytes: %" PRId64 "\n", tot.nbytes);
	printf("       #rows: %" PRId64 "\n", tot.nrows);
	printf("    #columns: ");
	if (tot.min_ncols == tot.max_ncols) {
		printf("%d\n", tot.min_ncols);
	} else {
		printf("%d .. %d\n", tot.min_ncols, tot.max_ncols);
	}
	printf("avg row size: %d\n", (int) (tot.nbytes / tot.nrows));
	printf("min row size: %d\n", tot.min_rowsz);
	printf("max row size: %d\n", tot.max_rowsz);
	
    return 0;
}
