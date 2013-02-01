DESTDIR?=
LDLIBS+=-lalpm

pacreport: pacreport.c

install: pacreport
	install -d ${DESTDIR}/usr/bin
	install -m 755 pacreport ${DESTDIR}/usr/bin/pacreport
