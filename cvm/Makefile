CC=cc
CFLAGS=-O2 -std=c11 -Iinclude -Wall -Wextra
LDFLAGS=

SRC=src/main.c src/loader.c src/vm.c src/http_host.c
OBJ=$(SRC:.c=.o)

all: cash

cash: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f $(OBJ) cash

.PHONY: all clean


