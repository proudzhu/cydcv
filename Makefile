OUT        = cydcv
VERSION    = 0.0.1

SRC        = cydcv.c
OBJ        = $(SRC:.c=.o)
DISTFILES  = Makefile cydcv.c

PREFIX    ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

CPPFLAGS  := -D_GNU_SOURCE -DCOWER_VERSION=\"$(VERSION)\" $(CPPFLAGS)
CFLAGS    := -std=c99 -g -pedantic -Wall -Wextra $(CFLAGS)
LDFLAGS   := $(LDFLAGS)
LDLIBS     = -lcurl -lyajl

all: $(OUT)

cower:
	${CC} $(CFLAGS) $(SRC) $(LDFLAGS) $(LDLIBS) -o $(OUT)

strip: $(OUT)
	strip --strip-all $(OUT)

install: all
	install -D -m755 cydcv "$(DESTDIR)$(PREFIX)/bin/cydcv"

uninstall:
	$(RM) "$(DESTDIR)$(PREFIX)/bin/cydcv"

dist: clean
	mkdir cydcv-$(VERSION)
	cp $(DISTFILES) cydcv-$(VERSION)
	sed "s/\(^VERSION *\)= .*/\1= $(VERSION)/" Makefile > cydcv-$(VERSION)/Makefile
	tar czf cydcv-$(VERSION).tar.gz cydcv-$(VERSION)
	rm -rf cydcv-$(VERSION)

distcheck: dist
	tar xf cydcv-$(VERSION).tar.gz
	$(MAKE) -C cydcv-$(VERSION)
	rm -rf cydcv-$(VERSION)

clean:
	$(RM) $(OUT) $(OBJ) $(MANPAGES)

.PHONY: clean dist install uninstall

