CC      = gcc
CFLAGS  = -O2 -Wall -Isrc -D_WIN32_WINNT=0x0601
LDFLAGS = -mwindows -lgdi32 -luser32 -lwinmm
SRCS    = $(wildcard src/*.c) $(wildcard src/modules/*.c)

deadlock.exe: $(SRCS) src/app.h
	$(CC) $(CFLAGS) $(SRCS) -o deadlock.exe $(LDFLAGS)

clean:
	rm -f deadlock.exe

.PHONY: clean
