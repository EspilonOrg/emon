CC      = gcc
VERSION ?= 0.1.1
PREFIX  ?= /usr/local
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
	install -d $(PREFIX)/bin/
	install -m 755 $(BIN) $(PREFIX)/bin/emon
	install -d $(PREFIX)/share/man/man1/
	install -m 644 docs/emon.1 $(PREFIX)/share/man/man1/emon.1
	install -d $(PREFIX)/share/emon/patterns/
	install -m 644 patterns/*.pat $(PREFIX)/share/emon/patterns/
	install -d $(PREFIX)/share/doc/emon/
	install -m 644 .emon.conf.example $(PREFIX)/share/doc/emon/

uninstall:
	rm -f $(PREFIX)/bin/emon
	rm -f $(PREFIX)/share/man/man1/emon.1
	rm -rf $(PREFIX)/share/emon/
	rm -rf $(PREFIX)/share/doc/emon/

check:
	@command -v pkg-config >/dev/null && pkg-config --exists libserialport \
		&& echo "libserialport: OK" \
		|| echo "libserialport: MISSING (apt install libserialport-dev)"
