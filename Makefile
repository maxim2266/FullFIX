SPEC := FIX44
BIN := fullfix-test

# targets
.PHONY: all release debug release32
release debug release32 : $(BIN)

all : clean release

# specification
# (http://stackoverflow.com/questions/2973445/gnu-makefile-rule-generating-a-few-targets-from-a-single-source-file)
include/$(SPEC).h test/$(SPEC).c : $(SPEC).done

.INTERMEDIATE : $(SPEC).done
$(SPEC).done : test/$(SPEC).xml tools/compile-spec
	tools/compile-spec -s test test/$(SPEC).xml

# compilation
CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Iinclude

release release32 : CFLAGS += -O3 -s -march=native -mtune=native \
-fomit-frame-pointer -Wl,--as-needed -flto=auto -ffunction-sections -fdata-sections -Wl,--gc-sections \
-DNDEBUG -DRELEASE -DUSE_SSE

debug : CFLAGS += -g -march=native -DDEBUG -DUSE_SSE

release32 : CFLAGS += -m32

SRC := src/parser.c src/scanner.c src/utils.c src/converters.c \
test/main.c test/scanner_test.c test/parser_test.c test/test_utils.c test/utils_test.c \
test/random_test.c test/$(SPEC).c

HEADERS := include/fix.h include/$(SPEC).h src/fix_impl.h test/test_utils.h

$(BIN) : $(SRC) $(HEADERS)
	$(CC) -o $@ $(CFLAGS) $(SRC)
	chmod 0711 $@

# clean-up
.PHONY : clean
clean :
	rm -f include/$(SPEC).h test/$(SPEC).c $(BIN)

