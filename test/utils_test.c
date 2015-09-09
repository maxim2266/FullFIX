/*
Copyright (c) 2015, Maxim Konakov
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define _GNU_SOURCE

#include "test_utils.h"
#include <time.h>
#include <string.h>

static
bool test_utc_timestamp_to_timeval()
{
	struct tm now;	// in UTC
	const time_t t = time(NULL);

	gmtime_r(&t, &now);

	char orig[200], converted[200];

	strftime(orig, sizeof(orig), "%F %T", &now);

	utc_timestamp utc =
	{
		.year = now.tm_year + 1900,
		.month = now.tm_mon + 1,
		.day = now.tm_mday,
		.hour = now.tm_hour,
		.minute = now.tm_min,
		.second = now.tm_sec,
		.millisecond = 123
	};

	struct timeval result;

	ENSURE(utc_timestamp_to_timeval(&utc, &result) == FE_OK, "Failed conversion to timeval");
	ENSURE(strftime(converted, sizeof(converted), "%F %T", gmtime(&result.tv_sec)) > 0, "Failed to convert resulting time_t");
	ENSURE(strcmp(converted, orig) == 0, "Time mismatch: original \"%s\", converted \"%s\"", orig, converted);
	ENSURE(utc.millisecond * 1000 == result.tv_usec, "Millisecond value mismatch");
	PASSED;
}

// all tests
void utils_test()
{
	puts("# Utils tests:");

	test_utc_timestamp_to_timeval();
}

