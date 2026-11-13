CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
CORE_PKGS = json-glib-1.0 gio-2.0 libcurl
PC_PKGS = gdk-pixbuf-2.0
GUI_PKGS = gtk+-3.0 $(CORE_PKGS)
EXE =
ifeq ($(OS),Windows_NT)
EXE = .exe
DESKTOP = Bench.exe
PC_LIBS = -luser32 -lgdi32
endif

.PHONY: all check smoke smoke-repair smoke-readable clean
all: visual-agent$(EXE) bench-engine$(EXE) $(DESKTOP)

Bench.exe: desktop.c
	$(CC) $(CFLAGS) -municode -mwindows desktop.c -o $@ -luser32

SOURCES = agent.c project.c runner.c
HEADERS = agent.h project.h runner.h

bench-engine$(EXE): engine.c pc_control.c pc_control.h pc_agent.c pc_agent.h $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell $(PKG_CONFIG) --cflags $(CORE_PKGS) $(PC_PKGS)) engine.c pc_control.c pc_agent.c $(SOURCES) -o $@ $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(CORE_PKGS) $(PC_PKGS)) $(PC_LIBS)

visual-agent$(EXE): main.c $(SOURCES) $(HEADERS) cursor.c cursor.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell $(PKG_CONFIG) --cflags $(GUI_PKGS)) main.c $(SOURCES) cursor.c -o $@ $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(GUI_PKGS)) -lm

test-agent$(EXE): test_agent.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell $(PKG_CONFIG) --cflags $(CORE_PKGS)) test_agent.c $(SOURCES) -o $@ $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(CORE_PKGS))

test-pc$(EXE): test_pc.c pc_control.c pc_control.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell $(PKG_CONFIG) --cflags $(CORE_PKGS) $(PC_PKGS)) test_pc.c pc_control.c -o $@ $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(CORE_PKGS) $(PC_PKGS)) $(PC_LIBS)

test-pc-agent$(EXE): test_pc_agent.c pc_agent.c pc_agent.h pc_control.c pc_control.h $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell $(PKG_CONFIG) --cflags $(CORE_PKGS) $(PC_PKGS)) test_pc_agent.c pc_agent.c pc_control.c $(SOURCES) -o $@ $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(CORE_PKGS) $(PC_PKGS)) $(PC_LIBS)

check: test-agent$(EXE) test-pc$(EXE) test-pc-agent$(EXE)
	./test-agent$(EXE)
	./test-pc$(EXE)
	./test-pc-agent$(EXE)

smoke: visual-agent$(EXE)
	./visual-agent$(EXE) --smoke

smoke-repair: visual-agent$(EXE)
	./visual-agent$(EXE) --smoke-repair

smoke-readable: visual-agent$(EXE)
	./visual-agent$(EXE) --smoke-readable

clean:
	rm -f visual-agent visual-agent.exe bench-engine bench-engine.exe test-agent test-agent.exe
