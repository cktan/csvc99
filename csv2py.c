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

const char *usagestr = "\n\
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
#include "csv.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *pname = 0;
const char *fname = 0;
int qte = '"';
int esc = '"';
int delim = ',';
char nullstr[20] = {0};

#define perr(M, ...) fprintf(stderr, M, ##__VA_ARGS__)
#define pout(M, ...) fprintf(stdout, M, ##__VA_ARGS__)
#define fatal(M, ...)                                                          \
  do {                                                                         \
    fprintf(stderr, M, ##__VA_ARGS__);                                         \
    exit(1);                                                                   \
  } while (0)

void usage(int exitcode, const char *msg) {
  perr(usagestr, pname);
  if (msg) {
    perr("\n%s\n", msg);
  }
  exit(exitcode);
}

void parse_cmdline(int argc, char *const *argv) {
  pname = argv[0];
  int opt;
  char *q, *e, *d, *n;
  q = e = d = n = 0;
  while ((opt = getopt(argc, argv, "d:q:e:n:h")) != -1) {
    switch (opt) {
    case 'd':
      d = optarg;
      break;
    case 'q':
      q = optarg;
      break;
    case 'e':
      e = optarg;
      break;
    case 'n':
      n = optarg;
      break;
    case 'h':
      usage(0, 0);
      break;
    default:
      usage(1, 0);
      break;
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
    esc = e[0];
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

void print_special(const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '\n':
      printf("\\n");
      break;

    case '\'':
    case '\\':
      putchar('\\');
    // fallthru
    default:
      putchar(*s);
    }
  }
}

int do_read(intptr_t handle, char *buf, int bufsz) {
  FILE *fp = (FILE *)handle;
  return fread(buf, 1, bufsz, fp);
}

int do_row(intptr_t handle, int64_t rownum, char **col, int ncol) {
  (void)handle;
  printf("%s", rownum > 1 ? ",\n" : "");
  printf("	[");
  for (int i = 0; i < ncol; i++) {
    char *s = col[i];
    printf("%s", i ? "," : "");
    if (s) {
      /* search of squote, backslash, or newline */
      if (strpbrk(s, "'\\\n")) {
        printf("'");
        print_special(col[i]);
        printf("'");
      } else {
        printf("'%s'", col[i]);
      }
    } else
      printf("None");
  }
  printf("]");

  return 0;
}

void do_error(intptr_t handle, int errtype, const char *errmsg,
              csv_parse_t *cp) {
  (void)handle;
  (void)errtype;
  errmsg = cp ? csv_errmsg(cp) : errmsg;
  fatal("ERROR: %s\n", errmsg);
}

int main(int argc, char *argv[]) {
  parse_cmdline(argc, argv);
  FILE *fp = stdin;

  if (fname && !(fp = fopen(fname, "r"))) {
    perr("ERROR: fopen %s - %s\n", fname, strerror(errno));
    exit(1);
  }

  printf("[\n");
  csv_scan((intptr_t)fp, qte, esc, delim, nullstr, do_read, do_row, do_error);
  printf("\n]\n\n");

  fclose(fp);

  return 0;
}
