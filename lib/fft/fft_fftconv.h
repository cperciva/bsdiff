/*-* Copyright (c) 2005, 2012 Colin Percival
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

#ifndef _FFT_FFTCONV_H_
#define _FFT_FFTCONV_H_

#include <stddef.h>

/**
 * fft_fftconv_scale(DAT, n):
 * Multiply the 2^n complex values (2^(n+1) doubles) stored in DAT by 2^(-n),
 * as needed to renormalize after an fft/mulpw/ifft convolution computation.
 */
void fft_fftconv_scale(double *, size_t);

/**
 * fft_fftconv_mulpw(DAT1, DAT2, n):
 * Compute the pairwise products of the 2^n complex values stored in DAT1 and
 * DAT2, and write the products into DAT1.
 */
void fft_fftconv_mulpw(double * restrict, double * restrict, size_t);

/**
 * fft_fftconv_sqrpw(DAT, n):
 * Compute the squares of the 2^n complex values stored in DAT and write them
 * back into DAT.
 */
void fft_fftconv_sqrpw(double *, size_t);

#endif /* !_FFT_FFTCONV_H_ */
