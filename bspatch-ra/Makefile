PROG=	bspatch-ra
SRCS=	main.c
LDADD=	-lbz2
NO_MAN=	YES
WARNS=	6

# bsdiff-ra code
.PATH.c	:	../lib/bsdiff-ra
SRCS	+=	bsdiff_ra_read.c
CFLAGS	+=	-I ../lib/bsdiff-ra

# libcperciva utility code
.PATH.c	:	../libcperciva/util
SRCS	+=	warnp.c
CFLAGS	+=	-I ../libcperciva/util

.include <bsd.prog.mk>
