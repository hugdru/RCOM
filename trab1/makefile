# source files.

SRC = $(wildcard src/*.c)

OBJ = $(patsubst src%, ../buildtemp%.o, $(SRC))

OUT = bin/programa

# include directories
INCLUDES = -I. -I./include/ -I./src -I/usr/include

# compiler
CCC = gcc -march=native -pipe

# library paths
LIBS = -Llib -L/usr/lib


# compile flags
LDFLAGS = -g

.SUFFIXES: .cpp

all: default
default: CCFLAGS = -std=c11 -O3
default: $(OUT)

# not valid for c++ -Wconversion-extra -Wnested-externs -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes
# -Wpadded
debug: CCFLAGS = -std=c11 -ggdb -Wall -Wextra -pedantic -Wdouble-promotion -Wshadow -Wfloat-equal -Wcast-align -Wcast-qual -Wwrite-strings -Wconversion -Wsign-conversion -Wuseless-cast -Wlogical-op -Wmissing-declarations -Wredundant-decls -Wdisabled-optimization -Wstack-protector -Winline -Wswitch-default -Wswitch-enum -Wconversion-extra -Wnexted-externs -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes -Wpadded

debug: $(OUT)

../buildtemp/%.o: src/%
	mkdir -p ../buildtemp
	$(CCC) $(INCLUDES) $(CCFLAGS) -c $< -o $@

$(OUT): $(OBJ)
	mkdir -p bin
	$(CCC) $(INCLUDES) $(CCFLAGS) $(OBJ) $(LIBS) -o $(OUT)

clean:
	rm -f $(OBJ) $(OUT)

test:
	echo $(SRC)
	echo $(OBJ)
