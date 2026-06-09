CC      = gcc
VERSION ?= 0.1.0
CFLAGS  = -Ivendor/include -Isrc -Wall -Wextra -std=c11 \
          -D_POSIX_C_SOURCE=200809L \
          -D_XOPEN_SOURCE=600 \
          -DVERSION_STR=\"$(VERSION)\" \
          -fstack-protector-strong
LDFLAGS = vendor/lib/libserialport.a -lpthread

ifeq ($(DEBUG), 1)
	CFLAGS += -g -O0 -DDEBUG
else
	CFLAGS += -O2
endif

ifeq ($(SANITIZE), 1)
	CFLAGS  += -fsanitize=address,undefined
	LDFLAGS += -fsanitize=address,undefined
endif

BIN  = espilon-monitor
SRCS = src/main.c \
       src/app/config.c src/app/daemon.c \
       src/monitor/detector.c src/monitor/monitor.c src/monitor/recorder.c \
       src/serial/serial.c src/serial/reset.c \
       src/ui/display.c src/ui/interactive.c src/ui/scrollback.c src/ui/tui.c \
       src/utils/utils.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean install uninstall check test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

TEST_SRCS = tests/test_detector.c src/monitor/detector.c src/utils/utils.c
TEST_BIN  = tests/test_detector

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $@ -lpthread

clean:
	rm -f $(OBJS) $(BIN) $(TEST_BIN)

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/
	@if [ -f docs/emon.1 ]; then \
	    install -d /usr/local/share/man/man1/; \
	    install -m 644 docs/emon.1 /usr/local/share/man/man1/; \
	fi

uninstall:
	rm -f /usr/local/bin/$(BIN)
	rm -f /usr/local/share/man/man1/emon.1

check:
	@command -v pkg-config >/dev/null && pkg-config --exists libserialport \
		&& echo "libserialport: OK" \
		|| echo "libserialport: MISSING (apt install libserialport-dev)"
