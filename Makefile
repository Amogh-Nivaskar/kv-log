CC     = gcc
CFLAGS = -g -Wall -Wextra

# ---- Find files automatically -------------------------------------------
# Main program
MAIN      = main.c
# Every file named test_*.c is a separate test program
TEST_SRCS = $(wildcard test_*.c)
# Every OTHER .c file is a subsystem (hashmap.c, and later storage.c, etc.)
LIB_SRCS  = $(filter-out $(MAIN) $(TEST_SRCS), $(wildcard *.c))

LIB_OBJS  = $(LIB_SRCS:.c=.o)
TESTS     = $(TEST_SRCS:.c=)

# ---- Targets ------------------------------------------------------------
all: kv $(TESTS)

# kv = kv.c + all subsystems
kv: main.o $(LIB_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# each test_X = test_X.c + all subsystems
test_%: test_%.o $(LIB_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# compile any .c into a .o (recompile if any header changes)
%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

# build and run every test
test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

clean:
	rm -f *.o kv $(TESTS)

.PHONY: all test clean
.SECONDARY:
