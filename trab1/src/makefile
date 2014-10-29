# source files.

SRC = $(wildcard src/*.c)

OBJ = $(patsubst src/%.c, ../buildtemp/%.o, $(SRC))

OUT = bin/serius

# compiler
CC = gcc

.SUFFIXES: .c

all: default
default: CFLAGS = -std=c11 -O2 -march=native -pipe
default: $(OUT)

debug: CFLAGS = -ggdb -g3 -Wall -Wextra -pedantic -Wdouble-promotion -Wshadow -Wfloat-equal -Wcast-align -Wcast-qual -Wwrite-strings -Wconversion -Wsign-conversion -Wlogical-op -Wmissing-declarations -Wredundant-decls -Wdisabled-optimization -Wstack-protector -Winline -Wswitch-default -Wswitch-enum -Wnested-externs -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes -Wpadded

debug: $(OUT)

../buildtemp/%.o: src/%.c
	mkdir -p ../buildtemp
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT): $(OBJ)
	mkdir -p bin
	$(CC) $(CFLAGS) $(OBJ) -o $(OUT)

clean:
	rm -f $(OBJ) $(OUT)

test:
	echo $(SRC)
	echo $(OBJ)
