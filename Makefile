CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -pedantic -std=c11
LDLIBS  = -lreadline -lpthread
AR      = ar
ARFLAGS = rcs

.PHONY: all trace memory-leak clean

all: biceps

# --- Build normal ---
libgescom.a: gescom.o
	$(AR) $(ARFLAGS) $@ $^

libcreme.a: creme.o
	$(AR) $(ARFLAGS) $@ $^

biceps: biceps.o libgescom.a libcreme.a
	$(CC) $(CFLAGS) -o $@ biceps.o -L. -lgescom -lcreme $(LDLIBS)

biceps.o: biceps.c gescom.h creme.h
	$(CC) $(CFLAGS) -c $<

gescom.o: gescom.c gescom.h
	$(CC) $(CFLAGS) -c $<

creme.o: creme.c creme.h
	$(CC) $(CFLAGS) -c $<

# --- Build valgrind ---
CFLAGS_MEM = -Wall -Wextra -Werror -pedantic -std=c11 -g -O0

libgescom_mem.a: gescom_mem.o
	$(AR) $(ARFLAGS) $@ $^

libcreme_mem.a: creme_mem.o
	$(AR) $(ARFLAGS) $@ $^

biceps-memory-leaks: biceps_mem.o libgescom_mem.a libcreme_mem.a
	$(CC) $(CFLAGS_MEM) -o $@ biceps_mem.o -L. -lgescom_mem -lcreme_mem $(LDLIBS)

biceps_mem.o: biceps.c gescom.h creme.h
	$(CC) $(CFLAGS_MEM) -c $< -o $@

gescom_mem.o: gescom.c gescom.h
	$(CC) $(CFLAGS_MEM) -c $< -o $@

creme_mem.o: creme.c creme.h
	$(CC) $(CFLAGS_MEM) -c $< -o $@

memory-leak: biceps-memory-leaks

# --- Trace ---
trace:
	$(MAKE) CFLAGS="$(CFLAGS) -DTRACE"

# --- Clean ---
clean:
	rm -f *.o *.a biceps biceps-memory-leaks