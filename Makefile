CC      = gcc
CFLAGS  = -Ivendor/include -Wall -Wextra -std=c11 \
          -D_POSIX_C_SOURCE=200809L \
          -D_XOPEN_SOURCE=600 \
          -Isrc
LDFLAGS = vendor/lib/libserialport.a -lpthread

ifeq ($(DEBUG), 1)
	CFLAGS += -g -O0 -DDEBUG
else
	CFLAGS += -O2
endif

BIN     = espilon-monitor
SRCS    = src/main.c \
          src/serial.c \
          src/detector.c \
          src/monitor.c \
          src/recorder.c \
          src/reset.c \
          src/display.c \
          src/config.c \
          src/interactive.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean install check test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

TEST_SRCS = tests/test_detector.c src/detector.c
TEST_BIN  = tests/test_detector

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $@ -lpthread

clean:
	rm -f $(OBJS) $(BIN) $(TEST_BIN)

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/

check:
	@command -v pkg-config >/dev/null && pkg-config --exists libserialport \
		&& echo "libserialport: OK" \
		|| echo "libserialport: MISSING (apt install libserialport-dev)"
