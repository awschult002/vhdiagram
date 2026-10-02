CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -pedantic -MMD
EXE     := vhdiagram$(if $(filter Windows_NT,$(OS)),.exe,)
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:.c=.o)

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)

test: $(EXE)
	sh tests/smoke.sh

clean:
	rm -f $(EXE) $(OBJ) $(OBJ:.o=.d)

-include $(OBJ:.o=.d)
.PHONY: all test clean
