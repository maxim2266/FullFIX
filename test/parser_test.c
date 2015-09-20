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
#include "FIX44.h"
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <math.h>
#include <errno.h>

// test support -------------------------------------------------------------------------------------------
static unsigned counter;						// for counting messages

#ifdef RELEASE
static struct timespec start_time, stop_time;	// for timing

static
void start()
{
	counter = 0;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start_time);
}

static
void stop()
{
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stop_time);
}

#define PRINT_TIMINGS()	if(res) print_times(__func__, NUM_MESSAGES, &start_time, &stop_time)
#endif	// #ifdef RELEASE

#define ENSURE_COUNTER(n)	\
	if(res && counter != (n))	\
	{	\
		REPORT_FAILURE("Invalid counter: expected %u, got %u", (n), counter);	\
		res = false;	\
	}	\
	else ((void)0)

// message validators -------------------------------------------------------------------------------------
static
bool simple_message_validator(const fix_parser_result* const res, const fix_string raw_msg UNUSED)
{
	++counter;
	ENSURE_PARSER_RESULT(res);
	ENSURE(fix_strings_equal(res->error.msg_type, CONST_LIT("D")), "Unexpected message type: \"%.*s\"",
		   (int)fix_string_length(res->error.msg_type), res->error.msg_type.begin);

	return valid_simple_message(res->root);
}

static
bool full_spec_simple_message_validator(const fix_parser_result* const res, const fix_string raw_msg UNUSED)
{
	++counter;
	ENSURE_PARSER_RESULT(res);
	ENSURE(fix_strings_equal(res->error.msg_type, CONST_LIT("D")), "Unexpected message type: \"%.*s\"",
		   (int)fix_string_length(res->error.msg_type), res->error.msg_type.begin);

	ENSURE(res->msg_type_code == NewOrderSingle, "Unexpected message type code %d", res->msg_type_code);
	return valid_simple_message(res->root);
}

static
bool full_spec_bin_message_validator(const fix_parser_result* const res, const fix_string raw_msg UNUSED)
{
	return full_spec_simple_message_validator(res, raw_msg)
		&& valid_string(res->root, XmlData, CONST_LIT("<blah-blah/>"));
}

static
bool group_message_validator(const fix_parser_result* const res, const fix_string raw_msg UNUSED)
{
	++counter;
	ENSURE_PARSER_RESULT(res);
	ENSURE(fix_strings_equal(res->error.msg_type, CONST_LIT("X")), "Unexpected message type: \"%.*s\"",
		   (int)fix_string_length(res->error.msg_type), res->error.msg_type.begin);

	return valid_message_with_groups(res->root);
}

static
bool full_spec_group_message_validator(const fix_parser_result* const res, const fix_string raw_msg UNUSED)
{
	++counter;
	ENSURE_PARSER_RESULT(res);
	ENSURE(fix_strings_equal(res->error.msg_type, CONST_LIT("X")), "Unexpected message type: \"%.*s\"",
		   (int)fix_string_length(res->error.msg_type), res->error.msg_type.begin);

	ENSURE(res->msg_type_code == MarketDataIncrementalRefresh, "Unexpected message type code %d", res->msg_type_code);
	return valid_message_with_groups(res->root);
}

static
bool duplicate_tag_validator(const fix_parser_result* const res, const fix_string raw_msg UNUSED)
{
	++counter;

	const fix_error_details* details = &res->error;

	ENSURE(details->code == FE_DUPLICATE_TAG, "Unexpected error %d", (int)details->code);
	ENSURE(details->tag == 269, "Unexpected error tag %u", details->tag);
	ENSURE(fix_strings_equal(details->context, CONST_LIT("269=")),
		   "Unexpected error context: \"%.*s\"", (int)fix_string_length(details->context), details->context.begin);

	return true;
}

static
bool mixed_messages_validator(const fix_parser_result* const res, const fix_string raw_msg UNUSED)
{
	++counter;

	if(res->error.code != FE_OK)
	{
		// must be bad_message_with_groups, with duplicate tag 269
		ENSURE(res->msg_type_code == MarketDataIncrementalRefresh, "Unexpected message type code %d", res->msg_type_code);

		const fix_error_details* details = &res->error;

		ENSURE(details->code == FE_DUPLICATE_TAG, "Unexpected error %d", (int)details->code);
		ENSURE(details->tag == 269, "Unexpected error tag %u", details->tag);
		ENSURE(fix_strings_equal(details->context, CONST_LIT("269=")),
			"Unexpected error context: \"%.*s\"", (int)fix_string_length(details->context), details->context.begin);

		return true;
	}
	else
	{
		ENSURE_PARSER_RESULT(res);

		switch(res->msg_type_code)
		{
			case MarketDataIncrementalRefresh:
				return valid_message_with_groups(res->root);
			case NewOrderSingle:
				return valid_simple_message(res->root);
			default:
				REPORT_FAILURE("Unexpected message type code %d", res->msg_type_code);
				return false;
		}
	}
}

// tests --------------------------------------------------------------------------------------------------
#ifdef RELEASE
#define NUM_MESSAGES 500000u
#else
#define NUM_MESSAGES 1000u
#endif

static
bool simple_test()
{
	counter = 0;

	bool res = parse_input_once(create_fix_parser(simple_message_parser_table, CONST_LIT("FIX.4.4")),
								simple_message,
								simple_message_validator);

	ENSURE_COUNTER(1);
	TEST_END(res);
}

static
bool group_test()
{
	counter = 0;

	bool res = parse_input_once(create_fix_parser(message_with_groups_parser_table, CONST_LIT("FIX.4.2")),
								message_with_groups,
								group_message_validator);

	ENSURE_COUNTER(1);
	TEST_END(res);
}

static
bool duplicate_tag_group_test()
{
	counter = 0;

	bool res = parse_input_once(create_fix_parser(message_with_groups_parser_table, CONST_LIT("FIX.4.4")),
								bad_message_with_groups,
								duplicate_tag_validator);

	ENSURE_COUNTER(1);
	TEST_END(res);
}

static
bool full_spec_simple_test()
{
	counter = 0;

	bool res = parse_input_once(create_FIX44_parser(), simple_message, full_spec_simple_message_validator);

	ENSURE_COUNTER(1);
	TEST_END(res);
}

static
bool full_spec_bin_test()
{
	counter = 0;

	bool res = parse_input_once(create_FIX44_parser(), simple_message_bin, full_spec_bin_message_validator);

	ENSURE_COUNTER(1);
	TEST_END(res);
}

static
bool full_spec_group_test()
{
	counter = 0;

	bool res = parse_input_once(create_FIX44_parser(), message_with_groups_4_4, full_spec_group_message_validator);

	ENSURE_COUNTER(1);
	TEST_END(res);
}

static
bool mixed_messages_full_spec_test()
{
	counter = 0;

	const fix_string msgs[] = { simple_message, message_with_groups_4_4, bad_message_with_groups };
	const size_t n_msgs = sizeof(msgs) / sizeof(msgs[0]);
	const fix_string input = make_n_copies_of_multiple_messages(NUM_MESSAGES / n_msgs,
																msgs,
																n_msgs);

	bool res = parse_input_once(create_FIX44_parser(), input, mixed_messages_validator);

	free((void*)input.begin);
	ENSURE_COUNTER((unsigned)((NUM_MESSAGES / n_msgs) * n_msgs));
	TEST_END(res);
}

#ifdef RELEASE

static
bool timed_simple_test()
{
	const fix_string input = make_n_copies(NUM_MESSAGES, simple_message);

	start();

	bool res = parse_input_once(create_fix_parser(simple_message_parser_table, CONST_LIT("FIX.4.4")),
								input,
								simple_message_validator);

	stop();
	free((void*)input.begin);
	ENSURE_COUNTER(NUM_MESSAGES);
	PRINT_TIMINGS();
	TEST_END(res);
}

static
bool timed_group_test()
{
	const fix_string input = make_n_copies(NUM_MESSAGES, message_with_groups);

	start();

	bool res = parse_input_once(create_fix_parser(message_with_groups_parser_table, CONST_LIT("FIX.4.2")),
								input,
								group_message_validator);

	stop();
	free((void*)input.begin);
	ENSURE_COUNTER(NUM_MESSAGES);
	PRINT_TIMINGS();
	TEST_END(res);
}

static
bool timed_simple_full_spec_test()
{
	const fix_string input = make_n_copies(NUM_MESSAGES, simple_message);

	start();

	bool res = parse_input_once(create_FIX44_parser(), input, full_spec_simple_message_validator);

	stop();
	free((void*)input.begin);
	ENSURE_COUNTER(NUM_MESSAGES);
	PRINT_TIMINGS();
	TEST_END(res);
}

static
bool timed_full_spec_group_test()
{
	const fix_string input = make_n_copies(NUM_MESSAGES, message_with_groups_4_4);

	start();

	bool res = parse_input_once(create_FIX44_parser(), input, full_spec_group_message_validator);

	stop();
	free((void*)input.begin);
	ENSURE_COUNTER(NUM_MESSAGES);
	PRINT_TIMINGS();
	TEST_END(res);
}

#endif	// #ifdef RELEASE

// all tests
void parser_test()
{
	puts("# Parser tests:");

	simple_test();
	group_test();
	duplicate_tag_group_test();
	full_spec_simple_test();
	full_spec_bin_test();
	full_spec_group_test();
	mixed_messages_full_spec_test();

#ifdef RELEASE
	timed_simple_test();
	timed_group_test();
	timed_simple_full_spec_test();
	timed_full_spec_group_test();
#endif	// #ifdef RELEASE
}
