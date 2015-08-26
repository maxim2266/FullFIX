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

// a little helper
static inline
unsigned min(unsigned a, unsigned b)
{
	return a < b ? a : b;
}

// macros for the scanner (unsafe!)
// error exit
#define ERROR_EXIT(name, t)	\
	EXIT_ ## name:	\
		parser->body_length = state.dest - parser->body;	\
		parser->result.error.context.end = state.dest;	\
		parser->result.error.code = FE_ ## name;	\
		parser->result.error.tag = (t);	\
		return false

// label name generator
#define __LABEL(name)	EXIT_ ## name
#define _LABEL(name)	__LABEL(name)

// error functions
#define FAIL()			goto _LABEL(ERROR_CONTEXT)
#define FAIL_IF(cond)	if(cond) FAIL()

// state macro
#define _STATE_LABEL(l) \
	case l: if(state.src == state.end) { parser->state_label = l; goto STATE_EXIT; } else ((void)0)

#define STATE_LABEL		_STATE_LABEL(__COUNTER__)

#define _READ_BYTE()	*state.dest++ = (c = CHAR_TO_INT(*state.src++))
#define _READ_BYTE_CS()	state.check_sum += CHAR_TO_INT(_READ_BYTE())

#define NEXT_BYTE()		STATE_LABEL; READ_BYTE()

#define MATCH_BYTE(x)	NEXT_BYTE(); FAIL_IF(c != CHAR_TO_INT(x))
#define MATCH_NEXT		NEXT_BYTE(); switch(c) {
#define END_MATCH		default: FAIL(); }

#define BEGIN	switch(parser->state_label) {
#define END		default: set_fatal_error(parser, FE_INVALID_PARSER_STATE); return false; }

// scanner function
bool extract_next_message(fix_parser* const parser)
{
	int c;
	scanner_state state = parser->state;

	BEGIN
#define READ_BYTE		_READ_BYTE_CS
#define ERROR_CONTEXT 	INVALID_BEGIN_STRING

		STATE_LABEL;
		state.dest = parser->body;

		// clear previous error
		parser->result.error = (fix_error_details){ FE_OK, 0, (fix_string){ state.dest, state.dest }, EMPTY_STR };
		parser->result.msg_type_code = -1;

		// copy begin string
		state.check_sum = 0;

		for(state.counter = parser->fix_version_len; ; )
		{
			const unsigned n = min(state.end - state.src, state.counter);
			const char* const end = state.src + n;

			while(state.src < end)
				state.check_sum += CHAR_TO_INT(*state.dest++ = *state.src++);

			if((state.counter -= n) == 0)
				break;

			STATE_LABEL;
		}

		// check begin string
		FAIL_IF(__builtin_memcmp(parser->fix_version, state.dest - parser->fix_version_len, parser->fix_version_len) != 0);

		// update context
		parser->result.error.context.begin = state.dest;

		// message length, goes to parser->counter
#undef  ERROR_CONTEXT
#define ERROR_CONTEXT 	INVALID_MESSAGE_LENGTH
		MATCH_NEXT
			case '1' ... '9': state.counter = c - '0'; break;
		END_MATCH

		NEXT_BYTE();

		while(c >= '0' && c <= '9')
		{
			state.counter = state.counter * 10 + c - '0';
			FAIL_IF(state.counter > MAX_MESSAGE_LENGTH);
			NEXT_BYTE();
		}

		// From this point on we expect at least MsgType(35), SenderCompID(49), TargetCompID(56) and
		// MsgSeqNum(34) tags to be present in every message, for example:
		// 35=0|49=X|56=Y|34=1|
		// which gives us the minimum message length
		FAIL_IF(c != SOH || state.counter < sizeof("35=0|49=X|56=Y|34=1|") - 1);

		// store context before possible reallocation
		parser->result.error.context.end = state.dest;

		// allocate space for message body
		state.dest = make_space(parser, state.dest, state.counter + sizeof("10=123|") - 1);

		if(!state.dest)
			return false;	// out of memory

		parser->frame.begin = state.dest;

		// copy message body, parser->counter bytes in total
		do
		{
			STATE_LABEL;

			const unsigned n = min(state.end - state.src, state.counter);
			const char* const end = state.src + n;

			while(state.src < end)
				state.check_sum += CHAR_TO_INT(*state.dest++ = *state.src++);

			state.counter -= n;
		} while(state.counter > 0);

		if(*(state.dest - 1) != SOH)	// not FAIL_IF() to preserve the context
		{
			set_error(&parser->result.error, FE_INVALID_MESSAGE_LENGTH, 9);
			return false;
		}

#undef  READ_BYTE
#define READ_BYTE		_READ_BYTE
#undef  ERROR_CONTEXT
#define ERROR_CONTEXT 	INVALID_TRAILER

		// CheckSum, goes to state.counter
		parser->frame.end = parser->result.error.context.begin = state.dest;

		MATCH_BYTE('1');
		MATCH_BYTE('0');
		MATCH_BYTE('=');

		MATCH_NEXT
			case '0' ... '2': state.counter = (c - '0') * 100; break;
		END_MATCH

		MATCH_NEXT
			case '0' ... '9': state.counter += (c - '0') * 10; break;
		END_MATCH

		MATCH_NEXT
			case '0' ... '9': state.counter += (c - '0'); break;
		END_MATCH

		MATCH_BYTE(SOH);

		// complete message body
		parser->body_length = state.dest - parser->body;

		// result
		if(state.counter == (0xFFu & state.check_sum))
			set_error_ctx(&parser->result.error, FE_OK, 0, EMPTY_STR);	// all fine
		else
		{	// invalid checksum - a recoverable error
			set_error(&parser->result.error, FE_INVALID_VALUE, 10);
			parser->result.error.context.end = state.dest - 1;
		}

		// save state
		parser->state = state;
		parser->state_label = 0;
		return true;
	END

STATE_EXIT:
	parser->state = state;
	return false;

	// exits
	ERROR_EXIT( INVALID_BEGIN_STRING, 	8 );
	ERROR_EXIT( INVALID_MESSAGE_LENGTH, 9 );
	ERROR_EXIT( INVALID_TRAILER, 		10 );
}
