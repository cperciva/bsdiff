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

/*
 * The value omc_s[2*k] + omc_s[2*k + 1] i is a correctly rounded value of
 * exp(2 pi i / 2^(k+7)) - 1 for 0 <= k <= 22.
 */
static double omc_s[] = {
	-0x1.3BC390D250439p-10, 0x1.91F65F10DD814p-5,
	-0x1.3BCFBD9979A27p-12, 0x1.92155F7A3667Ep-6,
	-0x1.3BD2C8DA49511p-14, 0x1.921D1FCDEC784p-7,
	-0x1.3BD38BAB6D94Cp-16, 0x1.921F0FE670071p-8,
	-0x1.3BD3BC5FC5AB4p-18, 0x1.921F8BECCA4BAp-9,
	-0x1.3BD3C88CDCA13p-20, 0x1.921FAAEE6472Ep-10,
	-0x1.3BD3CB98226DCp-22, 0x1.921FB2AECB360p-11,
	-0x1.3BD3CC5AF3E1Dp-24, 0x1.921FB49EE4EA6p-12,
	-0x1.3BD3CC8BA83EEp-26, 0x1.921FB51AEB57Cp-13,
	-0x1.3BD3CC97D5562p-28, 0x1.921FB539ECF31p-14,
	-0x1.3BD3CC9AE09BFp-30, 0x1.921FB541AD59Ep-15,
	-0x1.3BD3CC9BA36D7p-32, 0x1.921FB5439D73Ap-16,
	-0x1.3BD3CC9BD421Cp-34, 0x1.921FB544197A1p-17,
	-0x1.3BD3CC9BE04EEp-36, 0x1.921FB544387BAp-18,
	-0x1.3BD3CC9BE35A2p-38, 0x1.921FB544403C1p-19,
	-0x1.3BD3CC9BE41CFp-40, 0x1.921FB544422C2p-20,
	-0x1.3BD3CC9BE44DBp-42, 0x1.921FB54442A83p-21,
	-0x1.3BD3CC9BE459Dp-44, 0x1.921FB54442C73p-22,
	-0x1.3BD3CC9BE45CEp-46, 0x1.921FB54442CEFp-23,
	-0x1.3BD3CC9BE45DAp-48, 0x1.921FB54442D0Ep-24,
	-0x1.3BD3CC9BE45DDp-50, 0x1.921FB54442D16p-25,
	-0x1.3BD3CC9BE45DEp-52, 0x1.921FB54442D18p-26,
	-0x1.3BD3CC9BE45DEp-54, 0x1.921FB54442D18p-27,
};

/*
 * The value c_s[2*k] + c_s[2*k + 1] i is a correctly rounded value of
 * exp(2 pi k i / 2^6) for 0 <= k <= 8.
 */
static double c_s[] = {
	0x1.0000000000000p0, 0.0,
	0x1.FD88DA3D12526p-1, 0x1.917A6BC29B42Cp-4,
	0x1.F6297CFF75CB0p-1, 0x1.8F8B83C69A60Bp-3,
	0x1.E9F4156C62DDAp-1, 0x1.294062ED59F06p-2,
	0x1.D906BCF328D46p-1, 0x1.87DE2A6AEA963p-2,
	0x1.C38B2F180BDB1p-1, 0x1.E2B5D3806F63Bp-2,
	0x1.A9B66290EA1A3p-1, 0x1.1C73B39AE68C8p-1,
	0x1.8BC806B151741p-1, 0x1.44CF325091DD6p-1,
	0x1.6A09E667F3BCDp-1, 0x1.6A09E667F3BCDp-1,
};

/**
 * expm1_tbl(LUT, m):
 * Compute the values w^k - 1 where w = exp(2 pi i / 2^(m + 6)), and store the
 * real and imaginary parts into LUT[2 * k] and LUT[2 * k + 1] respectively,
 * for 0 <= k < 2^m.  m must be in the range [0, 23].
 */
static void
expm1_tbl(double * LUT, int m)
{
	double x0r, x0i, x1r, x1i;
	int n;
	size_t i, N;

	/* Sanity-check. */
	assert(m <= 23);

	/* LUT[0] + LUT[1] i = w^0 - 1 = 0. */
	LUT[0] = LUT[1] = 0.0;

	/*
	 * Using values computed for 0 <= k < 2^n - 1, fill in the values for
	 * 2^n <= k < 2^n + 2^n - 1, until we've covered every value k < 2^m.
	 */
	for (n = 0; n < m; n++) {
		N = 1 << n;

		/* x0r + x0i i = w^(2^n) - 1 = exp(2 pi i / 2^(m-n+6) - 1 */
		x0r = omc_s[2 * (m - n - 1)];
		x0i = omc_s[2 * (m - n - 1) + 1];

		/* w^(N+i)-1 = (w^N-1) + (w^i-1) + (w^N-1) * (w^i-1). */
		for (i = 0; i < N; i++) {
			x1r = LUT[2 * i];
			x1i = LUT[2 * i + 1];

			/*
			 * Note: Careful order of operations to minimize
			 * rounding errors.
			 */
			LUT[2 * (N + i)] = x0r + (x1r +
			    (x0r * x1r - x0i * x1i));
			LUT[2 * (N + i) + 1] = x0i + (x1i +
			    (x0r * x1i + x0i * x1r));
		}
	}
}

/**
 * roots_makelut(LUT, n):
 * Compute the values w^k where w = exp(2 * pi * i / 2^n) and store the real
 * and imaginary parts into LUT[2 * k] and LUT[2 * k + 1] respectively, for
 * 0 <= k < 2^(n-2).  LUT must be a pointer to sufficient space to store
 * 2^(n - 1) doubles.  n must be in the range [2, 29].
 *
 * The complex values stored are within 1.5 * 2^(-53) of the exact values.
 */
void
fft_roots_makelut(double * LUT, size_t n)
{
	double x0r, x0i, x1r, x1i;
	size_t i, k, N;

	/* Sanity-check. */
	assert(2 <= n && n <= 29);

	/* Special case for trivial table. */
	if (n == 2) {
		/* We only want k = 0 */
		LUT[0] = 1.0;
		LUT[1] = 0.0;
		return;
	}

	/* Fill in the first half of the table. */
	if (n <= 6) {
		/* Copy values of w^k for 0 <= k < 2^(n-3). */
		for (k = 0; k < (size_t)1 << (n - 3); k++) {
			LUT[2 * k] = c_s[2 * (k << (6 - n))];
			LUT[2 * k + 1] = c_s[2 * (k << (6 - n)) + 1];
		}
	} else {
		/* Compute w^k - 1 for 0 <= k < 2^(n-6). */
		expm1_tbl(LUT, n - 6);

		/*
		 * Combine with appropriate powers of exp(2 pi i / 2^6) to
		 * obtain w^k for 0 <= k < 2^(n-3).  We work with decending
		 * values of i since we end by overwriting the w^k - 1 values
		 * with w^k values.
		 */
		N = 1 << (n - 6);
		for (i = 7; i <= 7; i--) {
			for (k = 0; k < N; k++) {
				x0r = c_s[2 * i];
				x0i = c_s[2 * i + 1];
				x1r = LUT[2 * k];
				x1i = LUT[2 * k + 1];

				/* w^(N+k) = w^N + W^N * (w^k - 1). */
				LUT[2 * (i * N + k)] = x0r +
				    (x0r * x1r - x0i * x1i);
				LUT[2 * (i * N + k) + 1] = x0i +
				    (x0r * x1i + x0i * x1r);
			}
		}
	}

	/* Copy value of w^k = exp(i pi / 4) for k = 2^(n-3). */
	LUT[2 * (1 << (n - 3))] = c_s[2 * 8];
	LUT[2 * (1 << (n - 3)) + 1] = c_s[2 * 8 + 1];

	/* Fill in w^k for 2^(n-3) < k < 2^(n-2) by symmetry. */
	for (k = ((size_t)1 << (n - 3)) + 1; k < (size_t)1 << (n - 2); k++) {
		LUT[2 * k] = LUT[2 * (((size_t)1 << (n - 2)) - k) + 1];
		LUT[2 * k + 1] = LUT[2 * (((size_t)1 << (n - 2)) - k)];
	}
}
