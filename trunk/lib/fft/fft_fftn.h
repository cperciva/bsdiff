/*-
 * Copyright (c) 2012 Colin Percival
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

#ifndef _FFT_FFTN_H_
#define _FFT_FFTN_H_

#include <stddef.h>

/**
 * fft_fftn_getlen(N):
 * Return the power-of-2 FFT length used to compute length-N transforms.  The
 * value N must be less than 2^26.
 */
size_t fft_fftn_getlen(size_t);

/**
 * fft_fftn_makelut(LUT, N):
 * Inititalize a look-up table suitable for making calls to fftn_fft and
 * fftn_ifft.  LUT must be a pointer to sufficient space to store 4 * len
 * doubles, where len = fftn_getlen(N).
 */
void fft_fftn_makelut(double *, size_t);

/**
 * fft_fftn_fft(DAT, N, LUT, TMP):
 * Perform a length-N transform of the values z[k] = DAT[2*k] + DAT[2*k+1] i.
 * The table LUT must have been initialized by fftn_makelut(LUT, N), and TMP
 * must be a pointer to sufficient space to store 2 * len doubles, where len =
 * fftn_getlen(N).
 */
void fft_fftn_fft(double * restrict, size_t, double * restrict,
    double * restrict);

/**
 * fft_fftn_ifft(DAT, N, LUT, TMP):
 * Perform an inverse transform, as per fftn_fft.
 */
void fft_fftn_ifft(double * restrict, size_t, double * restrict,
    double * restrict);

#endif /* !_FFT_FFTN_H_ */
