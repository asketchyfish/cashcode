CC=cc
CFLAGS=-O2 -std=c11 -Iinclude -Wall -Wextra
LDFLAGS=

SRC=src/main.c src/loader.c src/vm.c src/http_host.c
OBJ=$(SRC:.c=.o)

all: cash

cash: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

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


