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

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "warnp.h"

#include "parallel_iter.h"

/* Management structure, passed to each thread. */
struct pstate {
	/* Parameters: Initialized and never modified. */
	size_t N;
	int (*func)(void *, size_t);
	void * cookie;

	/* Protects the rest of the structure. */
	pthread_mutex_t mtx;

	/* Next value of i to call func with. */
	size_t nexti;

	/* A function return code, if non-zero. */
	int rc;
};

/* Thread entry point. */
static void *
workthread(void * cookie)
{
	struct pstate * state = cookie;
	size_t i;
	int rc;

	/* Pick up the lock. */
	if ((errno = pthread_mutex_lock(&state->mtx)) != 0) {
		warnp("pthread_mutex_lock");
		goto err0;
	}

	/* Loop as long as there's work left to do. */
	while (state->nexti < state->N) {
		/* Grab some work. */
		i = state->nexti++;

		/* Drop the lock while we work. */
		if ((errno = pthread_mutex_unlock(&state->mtx)) != 0) {
			warnp("pthread_mutex_unlock");
			goto err0;
		}

		/* Call the function. */
		rc = (state->func)(state->cookie, i);

		/* Pick up the lock again. */
		if ((errno = pthread_mutex_lock(&state->mtx)) != 0) {
			warnp("pthread_mutex_lock");
			goto err0;
		}

		/* If the function returned a non-zero status, record it. */
		if (rc)
			state->rc = rc;
	}

	/* There's no work left to do.  Release the lock. */
	if ((errno = pthread_mutex_unlock(&state->mtx)) != 0) {
		warnp("pthread_mutex_unlock");
		goto err0;
	}

	/* Success! */
	return (state);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * parallel_iter(P, N, func, cookie):
 * Using P threads, invoke func(cookie, i) for each i in [0, N).  Return -1 on
 * internal error; zero if all the function calls return zero; or one of the
 * non-zero values returned by func.  On internal error there may still be
 * threads running and invoking func().
 */
int
parallel_iter(size_t P, size_t N, int (* func)(void *, size_t), void * cookie)
{
	struct pstate * state;
	pthread_t * threads;
	void * value_ptr;
	size_t i;
	int rc;

	/* Sanity-check. */
	assert(P > 0);

	/* Allocate space for thread IDs. */
	if ((threads = malloc(P * sizeof(pthread_t))) == NULL)
		goto err0;

	/*
	 * Allocate a state structure via malloc.  This could be placed on our
	 * stack if all goes well; but if an error occurs and we bail out with
	 * threads still running we don't want them to be referencing part of
	 * our (no-longer-valid) stack.
	 */
	if ((state = malloc(sizeof(struct pstate))) == NULL)
		goto err1;

	/* Initialize the state. */
	state->N = N;
	state->func = func;
	state->cookie = cookie;
	state->nexti = 0;
	state->rc = 0;

	/* Create state-locking mutex. */
	if ((errno = pthread_mutex_init(&state->mtx, NULL)) != 0) {
		warnp("pthread_mutex_init");
		goto err2;
	}

	/* Launch P threads. */
	for (i = 0; i < P; i++) {
		if ((errno = pthread_create(&threads[i], NULL,
		    workthread, state)) != 0) {
			warnp("pthread_create");
			goto err1;
		}
	}

	/* Wait for the threads to complete. */
	for (i = 0; i < P; i++) {
		/* Wait for the thread. */
		if ((errno = pthread_join(threads[i], &value_ptr)) != 0) {
			warnp("pthread_join");
			goto err1;
		}

		/* The thread returns NULL on internal error. */
		if (value_ptr == NULL)
			goto err1;
	}

	/* Don't need this mutex any more. */
	if ((errno = pthread_mutex_destroy(&state->mtx)) != 0) {
		warnp("pthread_mutex_destroy");
		goto err1;
	}

	/* Grab the return code from the state structure. */
	rc = state->rc;

	/*
	 * Free the state structure -- if we got here, there's no threads
	 * running any more so nobody will be holding a pointer to it.
	 */
	free(state);

	/* Free the array of thread IDs. */
	free(threads);

	/* Return the non-zero status code from a function call, if any. */
	return (rc);

err2:
	free(state);
err1:
	free(threads);
err0:
	/* Failure! */
	return (-1);
}
