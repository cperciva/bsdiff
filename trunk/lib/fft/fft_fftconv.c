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

#include "fft_fftconv.h"

/**
 * fft_fftconv_scale(DAT, n):
 * Multiply the 2^n complex values (2^(n+1) doubles) stored in DAT by 2^(-n),
 * as needed to renormalize after an fft/mulpw/ifft convolution computation.
 */
void
fft_fftconv_scale(double * DAT, size_t n)
{
	double s;
	size_t i;

	/* Compute 2^(-n). */
	for (s = 1.0, i = 0; i < n; i++)
		s = s * 0.5;

	/* Scale all the values. */
	for (i = 0; i < (size_t)2 << n; i++)
		DAT[i] = DAT[i] * s;
}

/**
 * fft_fftconv_mulpw(DAT1, DAT2, n):
 * Compute the pairwise products of the 2^n complex values stored in DAT1 and
 * DAT2, and write the products into DAT1.
 */
void
fft_fftconv_mulpw(double * restrict DAT1, double * restrict DAT2, size_t n)
{
	double xr, xi;
	size_t i;

	for (i = 0; i < (size_t)1 << n; i++) {
		xr = DAT1[i * 2];
		xi = DAT1[i * 2 + 1];

		DAT1[i * 2] = xr * DAT2[i * 2] - xi * DAT2[i * 2 + 1];
		DAT1[i * 2 + 1] = xr * DAT2[i * 2 + 1] + xi * DAT2[i * 2];
	}
}

/**
 * fft_fftconv_sqrpw(DAT, n):
 * Compute the squares of the 2^n complex values stored in DAT and write them
 * back into DAT.
 */
void
fft_fftconv_sqrpw(double * DAT, size_t n)
{
	double xr, xi;
	size_t i;

	for (i = 0; i < (size_t)1 << n; i++) {
		xr = DAT[i * 2];
		xi = DAT[i * 2 + 1];

		DAT[i * 2] = xr * xr - xi * xi;
		DAT[i * 2 + 1] = 2 * xr * xi;
	}
}
