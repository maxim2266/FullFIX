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

#include "fix_impl.h"

#ifdef USE_SSE
#include <xmmintrin.h>
#endif

// message buffer handling
static
char* make_space(fix_parser* const parser, char* dest, unsigned extra_len)
{
	const unsigned n = dest - parser->body, len = n + extra_len;

	if(len > parser->body_capacity)
	{
		// reallocate memory
		char* const p = realloc(parser->body, len);

		if(!p)
		{
			set_fatal_error(parser, FE_OUT_OF_MEMORY);
			return NULL;
		}

		// recalculate dest. pointer
		dest = p + n;

		// recalculate context
		parser->result.error.context = (fix_string){ p + (parser->result.error.context.begin - parser->body), dest };

		// store new pointer
		parser->body = p;
		parser->body_capacity = len;
	}

	return dest;
}

// initialisation
bool init_scanner(fix_parser* parser)
{
	char* const p = malloc(INITIAL_BODY_SIZE);

	if(!p)
		return false;

	parser->body = p;
	parser->body_capacity = INITIAL_BODY_SIZE;
	return true;
}

// scanner helper functions
static
unsigned min(unsigned a, unsigned b)
{
	return a < b ? a : b;
}

// copy a chunk of 'state->counter' bytes
static
bool copy_chunk(scanner_state* const state)
{
	const char* const s = state->src;
	const unsigned n = min(state->end - s, state->counter);

	state->dest = mempcpy(state->dest, s, n);
	state->src = s + n;
	state->counter -= n;
	return state->counter == 0;
}

static
unsigned char copy_cs(char* restrict p, const char* restrict s, unsigned n)
{
	unsigned char cs = 0;
	const char* const end = s + n;

#ifdef USE_SSE
	if(end - s >= 16)
	{
		__m128i cs128 = _mm_loadu_si128((const __m128i*)s);

		_mm_storeu_si128((__m128i*)p, cs128);

		for(s += 16, p += 16; end - s >= 16; s += 16, p += 16)
		{
			const __m128i tmp = _mm_loadu_si128((const __m128i*)s);

			cs128 = _mm_add_epi8(cs128, tmp);
			_mm_storeu_si128((__m128i*)p, tmp);
		}

		cs128 = _mm_add_epi8(cs128, _mm_srli_si128(cs128, 8));
		cs128 = _mm_add_epi8(cs128, _mm_srli_si128(cs128, 4));
		cs128 = _mm_add_epi8(cs128, _mm_srli_si128(cs128, 2));
		cs128 = _mm_add_epi8(cs128, _mm_srli_si128(cs128, 1));
		cs += _mm_extract_epi16(cs128, 0);	// SSE4: _mm_extract_epi8 ?
	}

	if(end - s >= 8)
	{
		__m128i cs64 = _mm_loadl_epi64((const __m128i*)s);

		_mm_storel_epi64((__m128i*)p, cs64);
		s += 8;
		p += 8;
		cs64 = _mm_add_epi8(cs64, _mm_srli_si128(cs64, 4));
		cs64 = _mm_add_epi8(cs64, _mm_srli_si128(cs64, 2));
		cs64 = _mm_add_epi8(cs64, _mm_srli_si128(cs64, 1));
		cs += _mm_extract_epi16(cs64, 0);	// SSE4: _mm_extract_epi8 ?
	}
#endif	// #ifdef USE_SSE

	while(s < end)
		cs += (*p++ = *s++);

	return cs;
}

static
bool copy_chunk_cs(scanner_state* const state)
{
	const unsigned n = min(state->end - state->src, state->counter);

	state->check_sum += copy_cs(state->dest, state->src, n);
	state->src += n;
	state->dest += n;
	state->counter -= n;
	return state->counter == 0;
}

// covert and validate message length
static
bool convert_message_length(scanner_state* const state)
{
	if(state->counter < 2)
		return false;

	const char* s = state->dest - state->counter;
	unsigned len = CHAR_TO_INT(*s++) - '0';

	if(len > 9)
		return false;

	const char* const end = state->dest - 1;

	while(s < end)
	{
		const unsigned t = CHAR_TO_INT(*s++) - '0';

		if(t > 9)
			return false;

		len = len * 10 + t;

		if(len > MAX_MESSAGE_LENGTH)
			return false;
	}

	if(len < sizeof("35=0|49=X|56=Y|34=1|") - 1)
		return false;

	state->counter = len;
	return true;
}

static
bool valid_checksum(const scanner_state* const state)
{
	const unsigned
		cs2 = CHAR_TO_INT(state->dest[-4]) - '0',
		cs1 = CHAR_TO_INT(state->dest[-3]) - '0',
		cs0 = CHAR_TO_INT(state->dest[-2]) - '0';

	return cs2 <= 9 && cs1 <= 9 && cs0 <= 9 && CHAR_TO_INT(state->check_sum) == cs2 * 100 + cs1 * 10 + cs0;
}

// scanner
bool extract_next_message(fix_parser* const parser)
{
	scanner_state* const state = &parser->state;

	switch(state->label)
	{
		case 0:	// initialisation
			// clear previous error
			parser->result.error = (fix_error_details){ FE_OK, 0, (fix_string){ parser->body, NULL }, EMPTY_STR };
			parser->result.msg_type_code = -1;

			// make new state
			state->dest = parser->body;
			state->counter = parser->header_len;

		case 1:	// message header
			// copy header
			if(state->src == state->end || !copy_chunk(state))
				return (state->label = 1, false);

			// validate header
			if(memcmp(parser->header, parser->body, parser->header_len) != 0)
				goto BEGIN_STRING_FAILURE;

			state->check_sum = parser->header_checksum;
			state->counter = 0;

			// update context
			parser->result.error.context.begin = state->dest;

		case 2:	// message length
			// copy bytes
			for(;;)
			{
				if(state->src == state->end)
					return (state->label = 2, false);

				const unsigned char x = (*state->dest++ = *state->src++);

				state->check_sum += x;
				++state->counter;

				if(x == SOH)
					break;

				if(state->counter == 10)	// max. 9 digits + SOH
					goto MESSAGE_LENGTH_FAILURE;
			}

			// convert
			if(!convert_message_length(state))
				goto MESSAGE_LENGTH_FAILURE;

			// store context
			parser->result.error.context.end = state->dest;

			// ensure enough space for the message body
			parser->frame.begin = state->dest = make_space(parser, state->dest, state->counter + sizeof("10=123|") - 1);

			if(!state->dest)
				return false;	// out of memory

		case 3: // message body
			// copy
			if(state->src == state->end || !copy_chunk_cs(state))
				return (state->label = 3, false);

			// validate
			if(*(state->dest - 1) != SOH)
			{
				set_error(&parser->result.error, FE_INVALID_MESSAGE_LENGTH, 9);	// preserving the error context
				return false;
			}

			// update context
			parser->frame.end = parser->result.error.context.begin = state->dest;

			// prepare for trailer
			state->counter = sizeof("10=123|") - 1;

		case 4: // trailer
			// copy
			if(state->src == state->end || !copy_chunk(state))
				return (state->label = 4, false);

			// complete message body
			parser->body_length = state->dest - parser->body;

			// validate
			if(state->dest[-7] != '1' || state->dest[-6] != '0' || state->dest[-5] != '=' || state->dest[-1] != SOH)
				goto TRAILER_FAILURE;

			// compare checksum
			if(!valid_checksum(state))
			{	// invalid checksum - a recoverable error
				set_error(&parser->result.error, FE_INVALID_VALUE, 10);
				parser->result.error.context.end = state->dest - 1;
			}
			else // all fine
				set_error_ctx(&parser->result.error, FE_OK, 0, EMPTY_STR);

			// all done
			state->label = 0;
			return true;

		default:
			set_fatal_error(parser, FE_INVALID_PARSER_STATE);
			return false;
	}

BEGIN_STRING_FAILURE:
	parser->result.error.code = FE_INVALID_BEGIN_STRING;
	parser->result.error.tag = 8;
	goto EXIT;

MESSAGE_LENGTH_FAILURE:
	parser->result.error.code = FE_INVALID_MESSAGE_LENGTH;
	parser->result.error.tag = 9;
	goto EXIT;

TRAILER_FAILURE:
	parser->result.error.code = FE_INVALID_TRAILER;
	parser->result.error.tag = 10;
	goto EXIT;

EXIT:
	parser->result.error.context.end = state->dest;
	parser->body_length = state->dest - parser->body;
	return false;
}



