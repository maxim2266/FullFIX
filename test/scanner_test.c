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
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <time.h>

// validators -----------------------------------------------------------------------------------------
static
bool simple_message_quick_validator(const fix_parser_result* const res, const fix_string raw_msg UNUSED)
{
	ENSURE_PARSER_RESULT(res);
	ENSURE(res->root, "Null root group");
	return true;
}

static
bool simple_message_ok(const fix_parser_result* const res, const fix_string raw_msg)
{
	if(!simple_message_quick_validator(res, raw_msg))
		return false;

	ENSURE(fix_strings_equal(raw_msg, simple_message), "Raw messages mismatch");
	ENSURE(fix_strings_equal(res->error.msg_type, CONST_LIT("D")), "Unexpected message type: \"%.*s\"",
		   (int)fix_string_length(res->error.msg_type), res->error.msg_type.begin);

	return valid_simple_message(res->root);
}

static
bool simple_message_invalid_checksum(const fix_parser_result* const res, const fix_string raw_msg UNUSED)
{
	const fix_error_details* details = &res->error;

	ENSURE(details->code == FE_INVALID_VALUE, "Unexpected error code %d", details->code);
	ENSURE(details->tag == 10, "Unexpected tag %u", details->tag);
	ENSURE(fix_strings_equal(details->context, CONST_LIT("10=172")),
			"Unexpected error context \"%.*s\"", (int)fix_string_length(details->context), details->context.begin);

	return true;
}

// helpers --------------------------------------------------------------------------------------------
static
bool invoke_twice_and_check(fix_parser* const parser, const size_t i)
{
	// first part, should yield nothing
	ENSURE(!get_first_fix_message(parser, simple_message.begin, i), "Unexpected parser result");

	const fix_error err = get_fix_parser_error_details(parser)->code;

	ENSURE(err == FE_OK, "Unexpected FIX parser error (%d): %s", (int)err, fix_error_to_string(err));

	// second part, should yield a complete message
	const fix_parser_result* res = get_first_fix_message(parser,
														 simple_message.begin + i,
														 fix_string_length(simple_message) - i);
	ENSURE_PARSER_RESULT(res);

	if(!simple_message_ok(res, get_raw_fix_message(parser)))
		return false;

	ENSURE(!get_next_fix_message(parser), "Unexpected FIX parser result");
	return true;
}

static
bool invoke_and_check_fatal_error(fix_parser* const parser, const fix_string msg, const fix_error expected)
{
	ENSURE(parser, "Null parser: %s", strerror(errno));
	ENSURE(!get_first_fix_message(parser, msg.begin, fix_string_length(msg)), "Unexpected parser result");

	const fix_error err = get_fix_parser_error_details(parser)->code;

	free_fix_parser(parser);
	ENSURE(err == expected, "Unexpected parser error (%d): %s", (int)err, fix_error_to_string(err));
	return true;
}

// tests ----------------------------------------------------------------------------------------------
static
bool simple_test()
{
	const bool ret = parse_input_once(create_fix_parser(simple_message_parser_table, CONST_LIT("FIX.4.4")),
									  simple_message,
									  simple_message_ok);
	TEST_END(ret);
}

static
bool simple_multiple_invocation_test()
{
	fix_parser* const parser = create_fix_parser(simple_message_parser_table, CONST_LIT("FIX.4.4"));

	ENSURE(parser, "Null parser: %s", strerror(errno));

	bool res = invoke_twice_and_check(parser, 1);

	for(size_t i = 2; res && i < fix_string_length(simple_message); ++i)
		res = invoke_twice_and_check(parser, i);

	free_fix_parser(parser);
	TEST_END(res);
}

static
bool invalid_header_test()
{
	const bool res = invoke_and_check_fatal_error(create_fix_parser(simple_message_parser_table, CONST_LIT("FIX.4.2")),
												  simple_message,
												  FE_INVALID_BEGIN_STRING);
	TEST_END(res);
}

static
bool invalid_checksum_test()
{
	const bool res = parse_input_once(create_fix_parser(simple_message_parser_table, CONST_LIT("FIX.4.4")),
									  bad_message_1,
									  simple_message_invalid_checksum);

	TEST_END(res);
}

static
bool invalid_message_length_test()
{
	const bool res = invoke_and_check_fatal_error(create_fix_parser(simple_message_parser_table, CONST_LIT("FIX.4.4")),
												  bad_message_2,
												  FE_INVALID_MESSAGE_LENGTH);
	TEST_END(res);
}

#ifdef RELEASE
#define NUM_MESSAGES 1000000

static
bool scanner_timed_test()
{
	const fix_string msgs = make_n_copies(NUM_MESSAGES, simple_message);
	fix_parser* const parser = create_fix_parser(simple_message_parser_table, CONST_LIT("FIX.4.4"));
	struct timespec start, stop;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);

	const bool res = parse_input_once(parser, msgs, simple_message_quick_validator);

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stop);
	free((void*)msgs.begin);
	print_times(__func__, NUM_MESSAGES, &start, &stop);
	TEST_END(res);
}

#endif

// all tests
void scanner_test()
{
	puts("# Scanner tests:");

	simple_test();
	simple_multiple_invocation_test();
	invalid_header_test();
	invalid_checksum_test();
	invalid_message_length_test();

#ifdef RELEASE
	scanner_timed_test();
#endif
}
