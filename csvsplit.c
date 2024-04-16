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
#define _GNU_SOURCE
#include "csv.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *g_pname = 0;
const char *g_prefix = "x";
int g_part = 0;
int64_t g_nbyte = 0;
int64_t g_nrec = 0;

#define perr(M, ...) fprintf(stderr, M, ##__VA_ARGS__)
#define pout(M, ...) fprintf(stdout, M, ##__VA_ARGS__)

static void fatal(const char *msg) {
  perr("%s\n", msg);
  exit(1);
}

static void outofmemory() {
  perr("ERROR: out of memory\n");
  exit(1);
}

static void prow(char *ptr, int len) {
  static FILE *fp = 0;
  static int64_t nb = 0;
  static int64_t nr = 0;

  if (fp && ((g_nbyte > 0 && nb + len > g_nbyte) || (g_nrec > 0 && nr >= g_nrec))) {
     fclose(fp);
     fp = 0;
     nb = 0;
     nr = 0;
  }

  if (!fp) {
    char fname[100];
    sprintf(fname, "%s0%d", g_prefix, g_part++);
    fp = fopen(fname, "w");
    if (!fp) {
      perror("fopen");
      fatal("ERROR: ... while trying to create output file\n");
      exit(1);
    }
    nb = nr = 0;
  }

  int n = fwrite(ptr, len, 1, fp);
  if (n != 1) {
    fatal("ERROR: cannot write to file\n");
  }

  nb += len;
  nr += 1;
}

void do_split(FILE *fp) {
  char nullstr[20];
  nullstr[0] = 0;
  csv_parse_t *cp = csv_open('"', '"', ',', nullstr);
  if (!cp) {
    fatal("csv_open failed");
  }

  int bufsz = 1024 * 64;
  char *buf = calloc(1, bufsz);
  char *p = buf;
  char *q = buf;

  if (!buf) {
    outofmemory();
  }

  while (!feof(fp)) {

    // shift forward
    if (p != buf) {
      memmove(buf, p, q - p);
      q = buf + (q - p);
      p = buf;
    }

    // expand
    if (p + bufsz == q) {
      if (bufsz >= 1024 * 1024 * 128) {
        fatal("ERROR: row bigger than 128MB\n");
      }
      char *tmp;
      int newsz = bufsz * 2;
      if (!(tmp = realloc(buf, newsz))) {
        outofmemory();
      }
      buf = tmp;
      bufsz = newsz;
    }

    // fill
    int avail = bufsz - (q - p);
    int n = fread(q, 1, avail, fp);
    if (n < 0) {
      perror("fread");
      exit(1);
    }
    q += n;

    // parse p..q
    while (p < q) {
      n = csv_line(cp, p, q - p);
      if (n < 0)
        fatal("ERROR: csv_feed failed\n");
      if (n == 0)
        break;
      prow(p, n);
      p += n;
    }
  }

  if (p < q) {
    prow(p, q - p);
  }

  free(buf);
  csv_close(cp);
}

void usage(int exitcode, const char *msg) {
  perr("usage: %s [OPTION] ... [FILE [PREFIX]]\n", g_pname);
  perr("\n");
  perr("    -h\n");
  perr("        print this help message\n");
  perr("    -b nbytes\n");
  perr("        split into files of at most nbytes each (at least one "
       "record)\n");
  perr("    -r nrecs\n");
  perr("        split into files of at most nrecs records each\n");
  perr("\n");
  perr("%s", msg ? msg : "");
  exit(exitcode);
}

int main(int argc, char *argv[]) {
  int opt;

  g_pname = argv[0];
  while ((opt = getopt(argc, argv, "hb:r:")) != -1) {
    switch (opt) {
    case 'h':
      usage(0, 0);
      break;
    case 'b':
      g_nbyte = strtol(optarg, 0, 0);
      if (g_nbyte <= 0) {
        usage(1,
              "ERROR: invalid -b nbytes option. Please supply a +ve integer\n");
      }
      break;
    case 'r':
      g_nrec = strtol(optarg, 0, 0);
      if (g_nrec <= 0) {
        usage(1,
              "ERROR: invalid -r nrecs option. Please supply a +ve integer\n");
      }
      break;
    default:
      usage(1, "ERROR: unknown option\n");
      break;
    }
  }

  if (g_nbyte > 0 && g_nrec > 0) {
    usage(1, "ERROR: specify only one of -b or -r options\n");
  }

  FILE *fp = stdin;
  if (optind < argc) {
    char *fname = argv[optind++];
    if (0 == strcmp(fname, "-")) {
      fp = stdin;
    } else if (!(fp = freopen(fname, "r", stdin))) {
      perror("fopen");
      exit(1);
    }
  }

  if (optind < argc) {
    g_prefix = argv[optind++];
  }

  if (optind != argc) {
    usage(1, "ERROR: unexpected arguments at end of command\n");
  }

  do_split(fp);
  return 0;
}
