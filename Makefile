CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
CORE_PKGS = json-glib-1.0 gio-2.0 libcurl
GUI_PKGS = gtk+-3.0 $(CORE_PKGS)
EXE =
ifeq ($(OS),Windows_NT)
EXE = .exe
DESKTOP = Bench.exe
endif

.PHONY: all check smoke smoke-repair smoke-readable clean
all: visual-agent$(EXE) bench-engine$(EXE) $(DESKTOP)

Bench.exe: desktop.c
	$(CC) $(CFLAGS) -municode -mwindows desktop.c -o $@ -luser32

SOURCES = agent.c project.c runner.c
HEADERS = agent.h project.h runner.h

bench-engine$(EXE): engine.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell $(PKG_CONFIG) --cflags $(CORE_PKGS)) engine.c $(SOURCES) -o $@ $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(CORE_PKGS))

visual-agent$(EXE): main.c $(SOURCES) $(HEADERS) cursor.c cursor.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell $(PKG_CONFIG) --cflags $(GUI_PKGS)) main.c $(SOURCES) cursor.c -o $@ $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(GUI_PKGS)) -lm

test-agent$(EXE): test_agent.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell $(PKG_CONFIG) --cflags $(CORE_PKGS)) test_agent.c $(SOURCES) -o $@ $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(CORE_PKGS))

check: test-agent$(EXE)
	./test-agent$(EXE)

smoke: visual-agent$(EXE)
	./visual-agent$(EXE) --smoke

smoke-repair: visual-agent$(EXE)
	./visual-agent$(EXE) --smoke-repair

smoke-readable: visual-agent$(EXE)
	./visual-agent$(EXE) --smoke-readable

clean:
	rm -f visual-agent visual-agent.exe bench-engine bench-engine.exe test-agent test-agent.exe
