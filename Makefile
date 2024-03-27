ARCH = $(shell uname -m)
CC = gcc-11
CFILES = csv.c
EXEC = csv2py csvsplit csvnorm csvstat csvecho

CFLAGS = -std=c99 -Wall -Wextra
ifeq ($(ARCH), x86_64)
	CFLAGS += -mavx2 -mfma -mbmi2
else ifeq ($(ARCH), aarch64)
	CFLAGS += -D__ARM_NEON__
endif

# to compile for debug: make DEBUG=1
# to compile for no debug: make
ifdef DEBUG
    CFLAGS += -O0 -g
else
    # NOTE: on gcc 6, csvstat produced by -O2 runs faster than -O3
    CFLAGS += -O3 -DNDEBUG
endif

LIB = libcsv.a

all: $(LIB) $(EXEC)

libcsv.a: csv.o
	ar -rcs $@ $^

csv2py: csv2py.c $(LIB)

csvsplit: csvsplit.c $(LIB)

csvnorm: csvnorm.c $(LIB)

csvstat: csvstat.c $(LIB)

csvecho: csvecho.c $(LIB)

format:
	clang-format -i $(shell find . -name '*.[ch]')

prefix ?= /usr/local

install: all
	install -d ${prefix}/include ${prefix}/lib
	install csv.h ${prefix}/include
	install libcsv.a ${prefix}/lib

clean:
	rm -f *.o $(EXEC) $(LIB)

.PHONY: all format install clean
