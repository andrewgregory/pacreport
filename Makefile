PREFIX       ?=  /usr/local
EXEC_PREFIX  ?=  $(PREFIX)
BINDIR       ?=  $(EXEC_PREFIX)/bin
DATAROOTDIR  ?=  $(PREFIX)/share
MANDIR       ?=  $(DATAROOTDIR)/man
MAN8DIR      ?=  $(MANDIR)/man8

DESTDIR?=
LDLIBS+=-lalpm

.PHONY: all
all: pacreport pacreport.1

pacreport: pacreport.c

pacreport.1: README.asciidoc
	a2x -f manpage --no-xmllint $^

install: pacreport pacreport.1
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 pacreport $(DESTDIR)$(BINDIR)/pacreport
	install -d $(DESTDIR)$(MAN8DIR)
	install -m 644 pacreport.1 $(DESTDIR)$(MAN8DIR)/pacreport.1

.PHONY: clean
clean:
	rm -f pacreport pacreport.1
