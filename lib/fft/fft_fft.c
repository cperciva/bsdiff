/*-
 * Copyright (c) 2005, 2012 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <assert.h>
#include <stddef.h>

#include "fft_roots.h"

#include "fft_fft.h"

/*
 * The FFT look-up table for has, for various values of N = 2^n, a sub-table
 * of 2^n doubles starting at offset 2^n.  Each sub-table of 2^n doubles is
 * used during the computation of length-2^(n+1) FFTs and consists of the
 * 2^(n-1) 2^(n+1)th roots of unity in the first quadrant.  (And the smaller
 * sub-tables are used during recursively-called FFTs, so they're indirectly
 * needed during larger FFTs too).
 */

/**
 * fft_fft_makelut(LUT, n):
 * Generate a look-up table suitable for use in computing FFTs of length up to
 * 2^n.  LUT must be a pointer to sufficient space to store 2^n doubles.  n
 * must be in the range [0, 29].
 */
void
fft_fft_makelut(double * LUT, size_t n)
{
	size_t i;

	/* Sanity-check. */
	assert(n <= 29);

	/*
	 * If we have a ridiculously small value of n, just fill in the table
	 * with zeroes -- we don't have subtables with less than one (complex
	 * double) entry each.
	 */
	LUT[0] = 0.0;
	if (n == 0)
		return;
	LUT[1] = 0.0;
	if (n == 1)
		return;

	/* Generate the table for length-2^n FFTs. */
	fft_roots_makelut(&LUT[2 << (n-2)], n);

	/* Generate the smaller tables by copying subsets of values. */
	for (i = (2 << (n - 2)) - 2; i >= 2; i -= 2) {
		LUT[i] = LUT[i * 2];
		LUT[i + 1] = LUT[i * 2 + 1];
	}
}

/*
 * The macro FFT_PM transforms (a, b) into (a + b, a - b), i.e., it performs a
 * length-2 FFT (which is also a length-2 inverse FFT).
 */
#define FFT_PM(a, b)	do {	\
	double t;		\
				\
	t = (b)[0];		\
	(b)[0] = (a)[0] - t;	\
	(a)[0] += t;		\
	t = (b)[1];		\
	(b)[1] = (a)[1] - t;	\
	(a)[1] += t;		\
} while (0)

/*
 * The macro FFT_SRM performs "split-radix mixing": It transforms (a, b, c, d)
 * into (a + c, b + d, (a - c) + (b - d) i, (a - c) - (b - d) i).  This
 * equates to one pass of FFT on half the data and two passes of FFT minus
 * twiddling on the other half of the data.  The macro IFFT_SRM performs the
 * inverse of FFT_SRM.
 */
#define FFT_SRM(a, b, c, d)	do {	\
	double t0r, t0i, t1r, t1i;	\
					\
	t0r = (a)[0] - (c)[0];		\
	(a)[0] += (c)[0];		\
	t0i = (a)[1] - (c)[1];		\
	(a)[1] += (c)[1];		\
	t1r = (b)[0] - (d)[0];		\
	(b)[0] += (d)[0];		\
	t1i = (b)[1] - (d)[1];		\
	(b)[1] += (d)[1];		\
					\
	(c)[0] = t0r - t1i;		\
	(d)[0] = t0r + t1i;		\
	(c)[1] = t0i + t1r;		\
	(d)[1] = t0i - t1r;		\
} while (0)

#define IFFT_SRM(a, b, c, d)	do {		\
	double t0r, t0i, t1r, t1i, t2r, t2i;	\
						\
	t2r = (c)[0];				\
	t2i = (c)[1];				\
	t0r = t2r + (d)[0];			\
	t0i = t2i + (d)[1];			\
	t1r = t2i - (d)[1];			\
	t1i = (d)[0] - t2r;			\
						\
	(c)[0] = (a)[0] - t0r;			\
	(a)[0] += t0r;				\
	(c)[1] = (a)[1] - t0i;			\
	(a)[1] += t0i;				\
	(d)[0] = (b)[0] - t1r;			\
	(b)[0] += t1r;				\
	(d)[1] = (b)[1] - t1i;			\
	(b)[1] += t1i;				\
} while (0)

/*
 * The macro FFT_SRM_PI_4 performs split-radix mixing and twiddling and
 * twiddling of four values: It transforms (a, b, c, d) into
 * (a + c, b + d, ((a - c) + (b - d) i) w, ((a - c) - (b - d) i) w^-1), where
 * w = (1 + i) / sqrt(2).  This is equivalent to FFT_SRM followed by rotating
 * c left by pi/4 and rotating d right by pi/4.  The macro IFFT_SRM_PI_4 is
 * the inverse of FFT_SRM_PI_4.
 */
#define SQRTHALF FFT_ROOTS_SQRTHALF

#define FFT_SRM_PI_4(a, b, c, d)	do {	\
	double t0r, t0i, t1r, t1i, t2r, t2i;	\
						\
	t0r = (a)[0] - (c)[0];			\
	(a)[0] += (c)[0];			\
	t0i = (a)[1] - (c)[1];			\
	(a)[1] += (c)[1];			\
	t1r = (b)[0] - (d)[0];			\
	(b)[0] += (d)[0];			\
	t1i = (b)[1] - (d)[1];			\
	(b)[1] += (d)[1];			\
						\
	t2r = t0r - t1i;			\
	t2i = t0i + t1r;			\
	t0r += t1i;				\
	t0i -= t1r;				\
						\
	(c)[0] = (t2r - t2i) * SQRTHALF;	\
	(c)[1] = (t2r + t2i) * SQRTHALF;	\
	(d)[0] = (t0r + t0i) * SQRTHALF;	\
	(d)[1] = (t0i - t0r) * SQRTHALF;	\
} while (0)

#define IFFT_SRM_PI_4(a, b, c, d)	do {	\
	double t0r, t0i, t1r, t1i, t2r, t2i;	\
						\
	t0r = ((c)[0] + (c)[1]) * SQRTHALF;	\
	t0i = ((c)[1] - (c)[0]) * SQRTHALF;	\
	t1r = ((d)[0] - (d)[1]) * SQRTHALF;	\
	t1i = ((d)[0] + (d)[1]) * SQRTHALF;	\
						\
	t2r = t0i - t1i;			\
	t2i = t1r - t0r;			\
	t0r += t1r;				\
	t0i += t1i;				\
						\
	(c)[0] = (a)[0] - t0r;			\
	(a)[0] += t0r;				\
	(c)[1] = (a)[1] - t0i;			\
	(a)[1] += t0i;				\
	(d)[0] = (b)[0] - t2r;			\
	(b)[0] += t2r;				\
	(d)[1] = (b)[1] - t2i;			\
	(b)[1] += t2i;				\
} while (0)

/*
 * The macro FFT_SRM_W performs split-radix mixing and twiddling and twiddling
 * of four values: It transforms (a, b, c, d) into
 * (a + c, b + d, ((a - c) + (b - d) i) w, ((a - c) - (b - d) i) w^-1), where
 * w is a root of unity.  The macro IFFT_SRM_W is the inverse of FFT_SRM_W.
 */
#define FFT_SRM_W(a, b, c, d, w)	do {	\
	double t0r, t0i, t1r, t1i, t2r, t2i;	\
						\
	t0r = (a)[0] - (c)[0];			\
	(a)[0] += (c)[0];			\
	t0i = (a)[1] - (c)[1];			\
	(a)[1] += (c)[1];			\
	t1r = (b)[0] - (d)[0];			\
	(b)[0] += (d)[0];			\
	t1i = (b)[1] - (d)[1];			\
	(b)[1] += (d)[1];			\
						\
	t2r = t0r - t1i;			\
	t2i = t0i + t1r;			\
	t0r += t1i;				\
	t0i -= t1r;				\
						\
	(c)[0] = t2r * (w)[0] - t2i * (w)[1];	\
	(c)[1] = t2i * (w)[0] + t2r * (w)[1];	\
	(d)[0] = t0r * (w)[0] + t0i * (w)[1];	\
	(d)[1] = t0i * (w)[0] - t0r * (w)[1];	\
} while (0)

#define IFFT_SRM_W(a, b, c, d, w)	do {	\
	double t0r, t0i, t1r, t1i, t2r, t2i;	\
						\
	t0r = (c)[0] * (w)[0] + (c)[1] * (w)[1];\
	t0i = (c)[1] * (w)[0] - (c)[0] * (w)[1];\
	t1r = (d)[0] * (w)[0] - (d)[1] * (w)[1];\
	t1i = (d)[1] * (w)[0] + (d)[0] * (w)[1];\
						\
	t2r = t0i - t1i;			\
	t2i = t1r - t0r;			\
	t0r += t1r;				\
	t0i += t1i;				\
						\
	(c)[0] = (a)[0] - t0r;			\
	(a)[0] += t0r;				\
	(c)[1] = (a)[1] - t0i;			\
	(a)[1] += t0i;				\
	(d)[0] = (b)[0] - t2r;			\
	(b)[0] += t2r;				\
	(d)[1] = (b)[1] - t2i;			\
	(b)[1] += t2i;				\
} while (0)

/*
 * Hard-coded small FFTs.
 */

static void
fft_0(double * restrict DAT, double * restrict LUT)
{

	(void)DAT; /* UNUSED */
	(void)LUT; /* UNUSED */

	/* Do nothing -- a length-1 FFT is a no-op. */
}

static void
fft_1(double * restrict DAT, double * restrict LUT)
{

	(void)LUT; /* UNUSED */

	FFT_PM(DAT, DAT + 2);
}

static void
fft_2(double * restrict DAT, double * restrict LUT)
{

	(void)LUT; /* UNUSED */

	FFT_SRM(DAT, DAT + 2, DAT + 4, DAT + 6);
	FFT_PM(DAT, DAT + 2);
}

static void
fft_3(double * restrict DAT, double * restrict LUT)
{

	(void)LUT; /* UNUSED */

	FFT_SRM(DAT, DAT + 4, DAT + 8, DAT + 12);
	FFT_SRM_PI_4(DAT + 2, DAT + 6, DAT + 10, DAT + 14);
	FFT_PM(DAT + 8, DAT + 10);
	FFT_PM(DAT + 12, DAT + 14);
	FFT_SRM(DAT, DAT + 2, DAT + 4, DAT + 6);
	FFT_PM(DAT, DAT + 2);
}

static void
fft_4(double * restrict DAT, double * restrict LUT)
{

	(void)LUT; /* UNUSED */

	FFT_SRM(DAT, DAT + 8, DAT + 16, DAT + 24);
	FFT_SRM_W(DAT + 2, DAT + 10, DAT + 18, DAT + 26, LUT + 10);
	FFT_SRM_PI_4(DAT + 4, DAT + 12, DAT + 20, DAT + 28);
	FFT_SRM_W(DAT + 6, DAT + 14, DAT + 22, DAT + 30, LUT + 14);
	FFT_SRM(DAT + 16, DAT + 18, DAT + 20, DAT + 22);
	FFT_PM(DAT + 16, DAT + 18);
	FFT_SRM(DAT + 24, DAT + 26, DAT + 28, DAT + 30);
	FFT_PM(DAT + 24, DAT + 26);
	FFT_SRM(DAT, DAT + 4, DAT + 8, DAT + 12);
	FFT_SRM_PI_4(DAT + 2, DAT + 6, DAT + 10, DAT + 14);
	FFT_PM(DAT + 8, DAT + 10);
	FFT_PM(DAT + 12, DAT + 14);
	FFT_SRM(DAT, DAT + 2, DAT + 4, DAT + 6);
	FFT_PM(DAT, DAT + 2);
}

/*
 * Generic split-radix FFT macro.
 */
#define FFT_FUNC(n, nm1, nm2)						\
static void fft_ ## n(double * restrict DAT, double * restrict LUT)	\
{									\
	size_t len = 1 << nm2;						\
	size_t i;							\
									\
	FFT_SRM(DAT, DAT + len * 2, DAT + len * 4, DAT + len * 6);	\
	for (i = 2; i < 2 * len; i += 2)				\
		FFT_SRM_W(DAT + i, DAT + len * 2 + i,			\
		    DAT + len * 4 + i, DAT + len * 6 + i,		\
		    LUT + len * 2 + i);					\
									\
	fft_ ## nm2(DAT + len * 4, LUT);				\
	fft_ ## nm2(DAT + len * 6, LUT);				\
	fft_ ## nm1(DAT, LUT);						\
}

/* Build all the functions. */
FFT_FUNC(5, 4, 3)
FFT_FUNC(6, 5, 4)
FFT_FUNC(7, 6, 5)
FFT_FUNC(8, 7, 6)
FFT_FUNC(9, 8, 7)
FFT_FUNC(10, 9, 8)
FFT_FUNC(11, 10, 9)
FFT_FUNC(12, 11, 10)
FFT_FUNC(13, 12, 11)
FFT_FUNC(14, 13, 12)
FFT_FUNC(15, 14, 13)
FFT_FUNC(16, 15, 14)
FFT_FUNC(17, 16, 15)
FFT_FUNC(18, 17, 16)
FFT_FUNC(19, 18, 17)
FFT_FUNC(20, 19, 18)
FFT_FUNC(21, 20, 19)
FFT_FUNC(22, 21, 20)
FFT_FUNC(23, 22, 21)
FFT_FUNC(24, 23, 22)
FFT_FUNC(25, 24, 23)
FFT_FUNC(26, 25, 24)
FFT_FUNC(27, 26, 25)
FFT_FUNC(28, 27, 26)
FFT_FUNC(29, 28, 27)

/* List of FFT functions. */
static void (* fft_list[])(double * restrict, double * restrict) = {
	fft_0, fft_1, fft_2, fft_3, fft_4, fft_5, fft_6, fft_7,
	fft_8, fft_9, fft_10, fft_11, fft_12, fft_13, fft_14, fft_15,
	fft_16, fft_17, fft_18, fft_19, fft_20, fft_21, fft_22, fft_23,
	fft_24, fft_25, fft_26, fft_27, fft_28, fft_29
};

/**
 * fft_fft_fft(DAT, n, LUT):
 * Compute a length-2^n FFT on the values DAT[2 * k] + DAT[2 * k + 1] i, using
 * the precomputed lookup table LUT.  The output is returned in DAT, in a
 * permuted order.  n must be in the range [0, 29] and no larger than the
 * value of n passed to fft_makelut to produce LUT.
 */
void
fft_fft_fft(double * restrict DAT, size_t size, double * restrict LUT)
{

	/* Sanity-check. */
	assert(size <= (sizeof(fft_list) / sizeof(fft_list[0])));

	fft_list[size](DAT, LUT);
}

/*
 * Hard-coded small IFFTs.
 */

static void
ifft_0(double * restrict DAT, double * restrict LUT)
{

	(void)DAT; /* UNUSED */
	(void)LUT; /* UNUSED */

	/* Do nothing -- a length-1 IFFT is a no-op. */
}

static void
ifft_1(double * restrict DAT, double * restrict LUT)
{

	(void)LUT; /* UNUSED */

	FFT_PM(DAT, DAT + 2);
}

static void
ifft_2(double * restrict DAT, double * restrict LUT)
{

	(void)LUT; /* UNUSED */

	FFT_PM(DAT, DAT + 2);
	IFFT_SRM(DAT, DAT + 2, DAT + 4, DAT + 6);
}

static void
ifft_3(double * restrict DAT, double * restrict LUT)
{

	(void)LUT; /* UNUSED */

	FFT_PM(DAT, DAT + 2);
	IFFT_SRM(DAT, DAT + 2, DAT + 4, DAT + 6);
	FFT_PM(DAT + 8, DAT + 10);
	FFT_PM(DAT + 12, DAT + 14);
	IFFT_SRM(DAT, DAT + 4, DAT + 8, DAT + 12);
	IFFT_SRM_PI_4(DAT + 2, DAT + 6, DAT + 10, DAT + 14);
}

static void
ifft_4(double * restrict DAT, double * restrict LUT)
{

	(void)LUT; /* UNUSED */

	FFT_PM(DAT, DAT + 2);
	IFFT_SRM(DAT, DAT + 2, DAT + 4, DAT + 6);
	FFT_PM(DAT + 8, DAT + 10);
	FFT_PM(DAT + 12, DAT + 14);
	IFFT_SRM(DAT, DAT + 4, DAT + 8, DAT + 12);
	IFFT_SRM_PI_4(DAT + 2, DAT + 6, DAT + 10, DAT + 14);
	FFT_PM(DAT + 16, DAT + 18);
	IFFT_SRM(DAT + 16, DAT + 18, DAT + 20, DAT + 22);
	FFT_PM(DAT + 24, DAT + 26);
	IFFT_SRM(DAT + 24, DAT + 26, DAT + 28, DAT + 30);
	IFFT_SRM(DAT, DAT + 8, DAT + 16, DAT + 24);
	IFFT_SRM_W(DAT + 2, DAT + 10, DAT + 18, DAT + 26, LUT + 10);
	IFFT_SRM_PI_4(DAT + 4, DAT + 12, DAT + 20, DAT + 28);
	IFFT_SRM_W(DAT + 6, DAT + 14, DAT + 22, DAT + 30, LUT + 14);
}

/*
 * Generic split-radix inverse FFT macro.
 */
#define IFFT_FUNC(n, nm1, nm2)						\
static void ifft_ ## n(double * restrict DAT, double * restrict LUT)	\
{									\
	size_t len = 1 << nm2;						\
	size_t i;							\
									\
	ifft_ ## nm1(DAT, LUT);						\
	ifft_ ## nm2(DAT + len * 4, LUT);				\
	ifft_ ## nm2(DAT + len * 6, LUT);				\
									\
	IFFT_SRM(DAT, DAT + len * 2, DAT + len * 4, DAT + len * 6);	\
	for (i = 2; i < 2 * len; i += 2)				\
		IFFT_SRM_W(DAT + i, DAT + len * 2 + i,			\
		    DAT + len * 4 + i, DAT + len * 6 + i,		\
		    LUT + len * 2 + i);					\
}

/* Build all the functions. */
IFFT_FUNC(5, 4, 3)
IFFT_FUNC(6, 5, 4)
IFFT_FUNC(7, 6, 5)
IFFT_FUNC(8, 7, 6)
IFFT_FUNC(9, 8, 7)
IFFT_FUNC(10, 9, 8)
IFFT_FUNC(11, 10, 9)
IFFT_FUNC(12, 11, 10)
IFFT_FUNC(13, 12, 11)
IFFT_FUNC(14, 13, 12)
IFFT_FUNC(15, 14, 13)
IFFT_FUNC(16, 15, 14)
IFFT_FUNC(17, 16, 15)
IFFT_FUNC(18, 17, 16)
IFFT_FUNC(19, 18, 17)
IFFT_FUNC(20, 19, 18)
IFFT_FUNC(21, 20, 19)
IFFT_FUNC(22, 21, 20)
IFFT_FUNC(23, 22, 21)
IFFT_FUNC(24, 23, 22)
IFFT_FUNC(25, 24, 23)
IFFT_FUNC(26, 25, 24)
IFFT_FUNC(27, 26, 25)
IFFT_FUNC(28, 27, 26)
IFFT_FUNC(29, 28, 27)

/* List of inverse FFT functions. */
static void (* ifft_list[])(double * restrict, double * restrict) = {
	ifft_0, ifft_1, ifft_2, ifft_3, ifft_4, ifft_5, ifft_6, ifft_7,
	ifft_8, ifft_9, ifft_10, ifft_11, ifft_12, ifft_13, ifft_14, ifft_15,
	ifft_16, ifft_17, ifft_18, ifft_19, ifft_20, ifft_21, ifft_22, ifft_23,
	ifft_24, ifft_25, ifft_26, ifft_27, ifft_28, ifft_29
};

/**
 * fft_fft_ifft(DAT, n, LUT):
 * Compute an (unnormalized) inverse FFT corresponding to fft_fft(DAT. n, LUT).
 */
void
fft_fft_ifft(double * restrict DAT, size_t size, double * restrict LUT)
{

	/* Sanity-check. */
	assert(size <= (sizeof(ifft_list) / sizeof(ifft_list[0])));

	ifft_list[size](DAT, LUT);
}
