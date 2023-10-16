CC=gcc
CFLAGS=-Wall -Werror -pedantic -std=gnu18 -g -pedantic -W -Wstrict-prototypes -Wunreachable-code  -Wpointer-arith -Wbad-function-cast -Wcast-align -lreadline

all: nish

nish: nish.c
	$(CC) -o $@ $^ $(CFLAGS)

run: nish
	./$<

pack: nish.c Makefile README.md
	tar cvzf nish.tar.gz $^
