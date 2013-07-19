CC?=            clang
AR?=            ar
INSTALL?=       install
PREFIX?=        /usr/local
CFLAGS?=        -O2 

all: librngd rngtest

librngd:
	$(CC) -c -I./src -I$(PREFIX)/include $(CFLAGS) -pthread -g -Wall -Werror ./src/fips.c ./src/stats.c ./src/util.c ./src/viapadlock_engine.c
	$(AR) rvs librngd.a fips.o stats.o util.o viapadlock_engine.o

rngtest:
	$(CC) -I./src -I/usr/include -I$(PREFIX)/include $(CFLAGS) -pthread -Wall -Werror ./src/rngtest.c -o rngtest $(PREFIX)/lib/libargp.a librngd.a

install:
	$(INSTALL) -m 755 -o root -g wheel rngtest $(PREFIX)/bin/
clean:
	rm -f *.o
	rm -f *.a
	rm -f rngtest

deinstall:
	rm -f $(PREFIX)/bin/rngtest

