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

#ifndef _FFT_ROOTS_H_
#define _FFT_ROOTS_H_

#include <stddef.h>

/*
 * The value FFT_ROOTS_SQRTHALF is the correctly rounded double precision
 * value of sqrt(1/2).  It is approximately 0.435 * 2^(-53) larger than the
 * exact value.
 */
#define FFT_ROOTS_SQRTHALF	0x1.6A09E667F3BCDp-1

/**
 * fft_roots_makelut(LUT, n):
 * Compute the values w^k where w = exp(2 * pi * i / 2^n) and store the real
 * and imaginary parts into LUT[2 * k] and LUT[2 * k + 1] respectively, for
 * 0 <= k < 2^(n-2).  LUT must be a pointer to sufficient space to store
 * 2^(n - 1) doubles.  n must be in the range [2, 29].
 *
 * The complex values stored are within 1.5 * 2^(-53) of the exact values.
 */
void fft_roots_makelut(double *, size_t);

#endif /* !_FFT_ROOTS_H_ */
