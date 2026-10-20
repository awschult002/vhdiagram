CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -pedantic -MMD
X       := $(if $(filter Windows_NT,$(OS)),.exe,)
EXE     := vhdiagram$(X)

# Headless core: everything in src/ except the program entry point.
CORE_SRC := $(filter-out src/main.c,$(wildcard src/*.c))
CORE_OBJ := $(CORE_SRC:.c=.o)
CORE_LIB := build/libvhcore.a
APP_OBJ  := src/main.o

TEST_SRC := $(wildcard tests/*_test.c)
TEST_OBJ := $(TEST_SRC:.c=.o)
TEST_BIN := $(patsubst tests/%.c,build/%$(X),$(TEST_SRC))

all: $(EXE)

$(EXE): $(APP_OBJ) $(CORE_LIB)
	$(CC) $(CFLAGS) -o $@ $(APP_OBJ) $(CORE_LIB) $(LDFLAGS) $(LDLIBS)

$(CORE_LIB): $(CORE_OBJ)
	@mkdir -p build
	rm -f $@
	$(AR) rcs $@ $(CORE_OBJ)

tests/%.o: CPPFLAGS += -Isrc

build/%$(X): tests/%.o $(CORE_LIB)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(CORE_LIB) $(LDFLAGS) $(LDLIBS)

test: $(EXE) $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "== $$t"; ./$$t || exit 1; done
	sh tests/smoke.sh

clean:
	rm -rf build
	rm -f $(EXE) $(APP_OBJ) $(CORE_OBJ) $(TEST_OBJ)
	rm -f $(APP_OBJ:.o=.d) $(CORE_OBJ:.o=.d) $(TEST_OBJ:.o=.d)

-include $(APP_OBJ:.o=.d) $(CORE_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
.PHONY: all test clean
.SECONDARY: $(TEST_OBJ)
