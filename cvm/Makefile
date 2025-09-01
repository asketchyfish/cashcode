CC=cc
CFLAGS=-O2 -std=c11 -Iinclude -Wall -Wextra
VERSION?=0.1.0
CPPFLAGS+=-DCASH_VERSION=\"$(VERSION)\"
LDFLAGS=

SRC=src/main.c src/loader.c src/vm.c src/http_host.c
OBJ=$(SRC:.c=.o)

all: cash

cash: $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJ) $(LDFLAGS)

PREFIX?=/usr/local
BINDIR?=$(PREFIX)/bin

install: cash
	mkdir -p $(DESTDIR)$(BINDIR)
	cp cash $(DESTDIR)$(BINDIR)/cash

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/cash

clean:
	rm -f $(OBJ) cash

.PHONY: all clean install uninstall


