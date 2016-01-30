PREFIX   ?= /usr
BINPREFIX = $(PREFIX)/bin

all: n30f

n30f: n30f.c
	gcc n30f.c -o n30f -lcairo -lxcb -lxcb-render

install: n30f
	mkdir -p "$(DESTDIR)$(BINPREFIX)"
	install -m 755 n30f "$(DESTDIR)$(BINPREFIX)"
	
clean:
	rm -f n30f
