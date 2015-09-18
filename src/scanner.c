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
static inline
unsigned min(unsigned a, unsigned b)
{
	return a < b ? a : b;
}

static
int next_char(scanner_state* const state)
{
	if(state->src == state->end)
		return EOF;

	const int c = CHAR_TO_INT(*state->src++);

	*state->dest++ = c;
	return c;
}

static
int next_char_cs(scanner_state* const state)
{
	const int c = next_char(state);

	if(c != EOF)
		state->check_sum += c;

	return c;
}

// copy a chunk of 'state->counter' bytes
static
bool copy_chunk(scanner_state* const state)
{
	if(state->src == state->end)
		return false;

	const unsigned n = min(state->end - state->src, state->counter);

	state->dest = __builtin_mempcpy(state->dest, state->src, n);
	state->src += n;
	state->counter -= n;
	return true;
}

static
bool copy_chunk_cs(scanner_state* const state)
{
	const char* s = state->src;

	if(s == state->end)
		return false;

	const unsigned n = min(state->end - s, state->counter);
	const char* const end = s + n;
	char* p = state->dest;
	unsigned char cs = 0;

	while(s < end)
		cs += (*p++ = *s++);

	state->src = s;
	state->dest = p;
	state->check_sum += cs;
	state->counter -= n;

	return true;
}

static const char checksum_tag[] = "10=";

// scanner
bool extract_next_message(fix_parser* const parser)
{
	int c;
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
			state->check_sum = parser->header_checksum;

		case 1:	// message header
			// copy header
			do
			{
				if(!copy_chunk(state))
					return (state->label = 1, false);
			} while(state->counter > 0);

			// validate header
			if(__builtin_memcmp(parser->header, parser->body, parser->header_len) != 0)
				goto BEGIN_STRING_FAILURE;

			// update context
			parser->result.error.context.begin = state->dest;

		case 2:	// message length (state->counter), first digit
			switch(c = next_char_cs(state))
			{
				case '1' ... '9':
					state->counter = c - '0';
					break;
				case EOF:
					return (state->label = 2, false);
				default:
					goto MESSAGE_LENGTH_FAILURE;
			}

		case 3:	// message length (state->counter), remaining digits
			do
			{
				switch(c = next_char_cs(state))
				{
					case '0' ... '9':
						state->counter = state->counter * 10 + c - '0';

						if(state->counter > MAX_MESSAGE_LENGTH)
							goto MESSAGE_LENGTH_FAILURE;
					case SOH:
						break;
					case EOF:
						return (state->label = 3, false);
					default:
						goto MESSAGE_LENGTH_FAILURE;
				}
			} while(c != SOH);

			// validate the length
			if(state->counter < sizeof("35=0|49=X|56=Y|34=1|") - 1)
				goto MESSAGE_LENGTH_FAILURE;

			// store context
			parser->result.error.context.end = state->dest;

			// ensure enough space for message body
			parser->frame.begin = state->dest = make_space(parser, state->dest, state->counter + sizeof("10=123|") - 1);

			if(!state->dest)
				return false;	// out of memory

		case 4: // message body
			do
			{
				if(!copy_chunk_cs(state))
					return (state->label = 4, false);
			} while(state->counter > 0);

			// validate
			if(*(state->dest - 1) != SOH)
			{
				set_error(&parser->result.error, FE_INVALID_MESSAGE_LENGTH, 9);	// preserving the error context
				return false;
			}

			// update context
			parser->frame.end = parser->result.error.context.begin = state->dest;

		case 5: // checksum tag
			do
			{
				if((c = next_char(state)) == EOF)
					return (state->label = 5, false);

				if(c != CHAR_TO_INT(checksum_tag[state->counter]))
					goto TRAILER_FAILURE;
			} while(++state->counter < sizeof(checksum_tag) - 1);	// initially state->counter is 0 from the previous state

		case 6: // checksum (state->counter), first digit
			switch(c = next_char(state))
			{
				case '0' ... '2':
					state->counter = c - '0';
					break;
				case EOF:
					return (state->label = 6, false);
				default:
					goto TRAILER_FAILURE;
			}

		case 7: // checksum (state->counter), second digit
			switch(c = next_char(state))
			{
				case '0' ... '9':
					state->counter = state->counter * 10 + c - '0';
					break;
				case EOF:
					return (state->label = 7, false);
				default:
					goto TRAILER_FAILURE;
			}

		case 8: // checksum (state->counter), third digit
			switch(c = next_char(state))
			{
				case '0' ... '9':
					state->counter = state->counter * 10 + c - '0';
					break;
				case EOF:
					return (state->label = 8, false);
				default:
					goto TRAILER_FAILURE;
			}

			// validate
			if(state->counter > 255)
				goto TRAILER_FAILURE;

		case 9: // final SOH
			if(state->src == state->end)
				return (state->label = 9, false);

			if((*state->dest++ = *state->src++) != SOH)
				goto TRAILER_FAILURE;

			// complete message body
			parser->body_length = state->dest - parser->body;

			// compare checksum
			if(state->counter == (unsigned)state->check_sum)
				set_error_ctx(&parser->result.error, FE_OK, 0, EMPTY_STR);	// all fine
			else
			{	// invalid checksum - a recoverable error
				set_error(&parser->result.error, FE_INVALID_VALUE, 10);
				parser->result.error.context.end = state->dest - 1;
			}

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



