CC       ?= gcc
PREFIX   ?= /usr
BINPREFIX = $(PREFIX)/bin

LDLIBS   += -lcairo -lxcb -lxcb-render

all: n30f

n30f: n30f.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

install: n30f
	mkdir -p "$(DESTDIR)$(BINPREFIX)"
	install -m 755 n30f "$(DESTDIR)$(BINPREFIX)"
	
clean:
	rm -f n30f
