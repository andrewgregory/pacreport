PREFIX       ?=  /usr/local
EXEC_PREFIX  ?=  $(PREFIX)
BINDIR       ?=  $(EXEC_PREFIX)/bin
DATAROOTDIR  ?=  $(PREFIX)/share
MANDIR       ?=  $(DATAROOTDIR)/man
MAN8DIR      ?=  $(MANDIR)/man8

DESTDIR?=
LDLIBS+=-lalpm

.PHONY: all
all: pacreport pacreport.8

pacreport: pacreport.c

pacreport.8: README.asciidoc
	a2x -f manpage --no-xmllint $^

install: pacreport
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 pacreport $(DESTDIR)$(BINDIR)/pacreport
	install -d $(DESTDIR)$(MAN8DIR)
	install -m 644 pacreport.8 $(DESTDIR)$(MAN8DIR)/pacreport.8

.PHONY: clean
clean:
	rm -f pacreport pacreport.8
