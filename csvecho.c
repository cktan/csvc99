#include "csv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s csv-encoded-string\n", argv[0]);
    exit(1);
  }

  char *line = argv[1];

  char **field;
  int nfield;
  char nullstr[20];
  nullstr[0] = 0;
  ;
  csv_parse_t *cp = csv_open('\"', '\\', '|', nullstr);

  int n = csv_feed_last(cp, line, strlen(line), &field, &nfield);
  if (n == -1) {
    fprintf(stderr, "cannot parse %s -- %s\n", line, csv_errmsg(cp));
    exit(1);
  }

  if (n == 0) {
    fprintf(stderr, "cannot parse %s -- incomplete line\n", line);
    exit(1);
  }

  if (nfield != 1) {
    fprintf(stderr, "nfield expected to be 1, but is %d instead\n", nfield);
    exit(1);
  }

  printf("%s\n", field[0]);

  csv_close(cp);
  return 0;
}
