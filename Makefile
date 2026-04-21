# Outils et options de compilation communes.
CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -pedantic -std=c11
LDLIBS  = -lreadline -lpthread
AR      = ar
ARFLAGS = rcs

# Cibles utilitaires (pas de fichiers reels).
.PHONY: all trace clean

# Cible par defaut: construit l'executable principal.
all: biceps

# Construction des bibliotheques statiques.
libgescom.a: gescom.o
	$(AR) $(ARFLAGS) $@ $^

libcreme.a: creme.o
	$(AR) $(ARFLAGS) $@ $^

biceps: biceps.o libgescom.a libcreme.a
	$(CC) $(CFLAGS) -o $@ biceps.o -L. -lgescom -lcreme $(LDLIBS)

# Compilation unitaire des modules.
biceps.o: biceps.c gescom.h creme.h
	$(CC) $(CFLAGS) -c $<

gescom.o: gescom.c gescom.h
	$(CC) $(CFLAGS) -c $<

creme.o: creme.c creme.h
	$(CC) $(CFLAGS) -c $<

trace:
	$(MAKE) CFLAGS="$(CFLAGS) -DTRACE"

# Nettoyage des artefacts de build.
clean:
	rm -f *.o *.a biceps
