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

#ifndef _FFT_FFT_H_
#define _FFT_FFT_H_

#include <stddef.h>

/**
 * fft_fft_makelut(LUT, n):
 * Generate a look-up table suitable for use in computing FFTs of length up to
 * 2^n.  LUT must be a pointer to sufficient space to store 2^n doubles.  n
 * must be in the range [0, 29].
 */
void fft_fft_makelut(double *, size_t);

/**
 * fft_fft_fft(DAT, n, LUT):
 * Compute a length-2^n FFT on the values DAT[2 * k] + DAT[2 * k + 1] i, using
 * the precomputed lookup table LUT.  The output is returned in DAT, in a
 * permuted order.  n must be in the range [0, 29] and no larger than the
 * value of n passed to fft_makelut to produce LUT.
 */
void fft_fft_fft(double * restrict, size_t, double * restrict);

/**
 * fft_fft_ifft(DAT, n, LUT):
 * Compute an (unnormalized) inverse FFT corresponding to fft_fft(DAT. n, LUT).
 */
void fft_fft_ifft(double * restrict, size_t, double * restrict);

#endif /* !_FFT_FFT_H_ */
