PROG=	bsdiff-big
SRCS=	main.c
LDADD=	-lm -lbz2 -lpthread
NO_MAN=	YES
WARNS=	6

# bsdiff code
.PATH.c	:	../lib/bsdiff
SRCS	+=	bsdiff_align.c
SRCS	+=	bsdiff_align_multi.c
SRCS	+=	bsdiff_writepatch.c
CFLAGS	+=	-I ../lib/bsdiff

# Block matching code
.PATH.c	:	../lib/blockmatch
SRCS	+=	blockmatch_index.c
SRCS	+=	blockmatch_psimm.c
CFLAGS	+=	-I ../lib/blockmatch

# FFT code
.PATH.c	:	../lib/fft
SRCS	+=	fft_fft.c
SRCS	+=	fft_fftconv.c
SRCS	+=	fft_fftn.c
SRCS	+=	fft_roots.c
CFLAGS	+=	-I ../lib/fft

# Parallelization code
.PATH.c	:	../lib/parallel
SRCS	+=	parallel_iter.c
CFLAGS	+=	-I ../lib/parallel

# Suffix sorting code
.PATH.c	:	../lib/sufsort
SRCS	+=	sufsort_qsufsort.c
CFLAGS	+=	-I ../lib/sufsort

# Utility code
.PATH.c	:	../lib/util
SRCS	+=	mapfile.c
CFLAGS	+=	-I ../lib/util

# libcperciva data structure code
.PATH.c	:	../libcperciva/datastruct
SRCS	+=	elasticarray.c
CFLAGS	+=	-I ../libcperciva/datastruct

# libcperciva utility code
.PATH.c	:	../libcperciva/util
SRCS	+=	entropy.c
SRCS	+=	warnp.c
CFLAGS	+=	-I ../libcperciva/util

.include <bsd.prog.mk>
