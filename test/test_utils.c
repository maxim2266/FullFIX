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
#include <malloc.h>
#include <error.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#define NONE ((unsigned)-1)

// utils
void* check_ptr(void* p)
{
	if(!p)
		error(EXIT_FAILURE, ENOMEM, "NULL pointer");

	return p;
}

fix_string make_n_copies(size_t n, const fix_string src)
{
	// assuming n > 0
	const size_t len = n * fix_string_length(src);
	char* const msg = check_ptr(malloc(len + 1));
	char* p = mempcpy(msg, src.begin, fix_string_length(src));

	while(--n > 0)
		p = mempcpy(p, src.begin, fix_string_length(src));

	*p = 0;

	return (fix_string){ msg, msg + len };
}

fix_string make_n_copies_of_multiple_messages(size_t n, const fix_string src[], size_t n_src)
{
	// 'n' is the total number of 'src' packs, not messages!
	// calculate the number of bytes to allocate
	size_t len = 0;

	for(size_t i = 0; i < n_src; ++i)
		len += fix_string_length(src[i]);

	len *= n;

	// allocate memory and copy messages
	char* const msg = check_ptr(malloc(len + 1));
	char* p = msg;

	while(n-- > 0)
		for(size_t j = 0; j < n_src; ++j)
			p = mempcpy(p, src[j].begin, fix_string_length(src[j]));

	*p = 0;

	// done
	return (fix_string){ msg, msg + len };
}

bool equal_utc_timestamps(const utc_timestamp* ts1, const utc_timestamp* ts2)
{
	return ts1->year == ts2->year
		&& ts1->month == ts2->month
		&& ts1->day == ts2->day
		&& ts1->hour == ts2->hour
		&& ts1->minute == ts2->minute
		&& ts1->second == ts2->second
		&& ts1->millisecond == ts2->millisecond;
}

void print_times(const char* test_name, size_t num_messages,
				 const struct timespec* start, const struct timespec* stop)
{
	const double
	t = (stop->tv_sec + stop->tv_nsec * 1e-9) - (start->tv_sec + start->tv_nsec * 1e-9),
	rate = num_messages / (1000. * t);

	printf("-- %s: %zuK messages in %0.3f s (%.0fK msg/s, %.3f us/msg)\n",
		   test_name, num_messages / 1000, t, rate, 1000. / rate);
}

void report_error_details(const fix_error_details* const details, const char* file_name, int line_no)
{
	const char* const msg = compose_fix_error_message(details);

	if(msg)
	{
		fprintf(stderr, "%s:%d: %s\n", file_name, line_no, msg);
		free((void*)msg);
	}
	else
		fprintf(stderr, "%s:%d: Cannot compose error message (errno = %d), parser error (%d): %s\n",
				file_name, line_no, errno, (int)details->code, fix_error_to_string(details->code));

	fflush(stderr);
}

bool parser_result_ok(const fix_parser_result* const res, const char* file_name, int line_no)
{
	if(!res)
	{
		fprintf(stderr, "%s:%d: Null FIX parser result\n", file_name, line_no);
		return false;
	}

	if(res->error.code != FE_OK)
	{
		report_error_details(&res->error, file_name, line_no);
		return false;
	}

	return true;
}

// typed validators ---------------------------------------------------------------------------------
bool valid_string(fix_group* const group, unsigned tag, const fix_string expected)
{
	fix_string value = { NULL, NULL };

	GET_TAG(group, tag, value, get_fix_tag_as_string);

	ENSURE(fix_strings_equal(value, expected),
		   "Tag %u - value mismatch: expected \"%.*s\", got \"%.*s\"",
		   tag, (int)fix_string_length(expected), expected.begin, (int)fix_string_length(value), value.begin);

	return true;
}

bool valid_long(fix_group* group, unsigned tag, const long expected)
{
	long value = 0;

	GET_TAG(group, tag, value, get_fix_tag_as_long);
	ENSURE(value == expected, "Tag %u - value mismatch: expected %ld, got %ld", tag, expected, value);
	return true;
}

bool valid_double(fix_group* group, unsigned tag, const double expected)
{
	double value = 0;

	GET_TAG(group, tag, value, get_fix_tag_as_double);
	ENSURE(fabs((value - expected) / expected) < 1.e-6,
		   "Tag %u - value mismatch: expected %f, got %f", tag, expected, value);

	return true;
}

bool valid_char(fix_group* group, unsigned tag, const char expected)
{
	char value = 0;

	GET_TAG(group, tag, value, get_fix_tag_as_char);
	ENSURE(value == expected, "Tag %u - value mismatch: expected '%c', got '%c'", tag, expected, value);
	return true;
}

bool valid_boolean(fix_group* group, unsigned tag, const bool expected)
{
	bool value = false;

	GET_TAG(group, tag, value, get_fix_tag_as_boolean);
	ENSURE(value == expected, "Tag %u - value mismatch: expected %d, got %d", tag, (int)expected, (int)value);
	return true;
}

bool valid_timestamp(fix_group* group, unsigned tag, const utc_timestamp* expected)
{
	utc_timestamp value = { 0, 0, 0, 0, 0, 0, 0 };

	GET_TAG(group, tag, value, get_fix_tag_as_utc_timestamp);
	ENSURE(equal_utc_timestamps(&value, expected), "Tag %u - value mismatch: timestamps", tag);
	return true;
}

// parser invocation --------------------------------------------------------------------------------
bool parse_input(fix_parser* const parser, const fix_string input, message_function f)
{
	for(const fix_parser_result* res = get_first_fix_message(parser, input.begin, fix_string_length(input));
		res;
		res = get_next_fix_message(parser))
	{
		if(!f(res, get_raw_fix_message(parser)))
			return false;
	}

	const fix_error_details* const error = get_fix_parser_error_details(parser);

	if(error->code <= FE_OTHER)
		return true;

	report_error_details(error, __FILE__, __LINE__);
	return false;
}

bool parse_input_once(fix_parser* const parser, const fix_string input, message_function f)
{
	ENSURE(parser, "Null parser: %s", strerror(errno));

	const bool ret = parse_input(parser, input, f);

	free_fix_parser(parser);
	return ret;
}

// Messages
const fix_string
simple_message = LIT("8=FIX.4.4\x01" "9=122\x01" "35=D\x01" "34=215\x01" "49=CLIENT12\x01"
					 "52=20100225-19:41:57.316\x01" "56=B\x01" "1=Marcel\x01" "11=13346\x01"
					 "21=1\x01" "40=2\x01" "44=5\x01" "54=1\x01" "59=0\x01" "60=20100225-19:39:52.020\x01"
					 "10=072\x01"),
bad_message_1 = LIT("8=FIX.4.4\x01" "9=122\x01" "35=D\x01" "34=215\x01" "49=CLIENT12\x01"
					 "52=20100225-19:41:57.316\x01" "56=B\x01" "1=Marcel\x01" "11=13346\x01"
					 "21=1\x01" "40=2\x01" "44=5\x01" "54=1\x01" "59=0\x01" "60=20100225-19:39:52.020\x01"
					 "10=172\x01"),	// wrong checksum
bad_message_2 = LIT("8=FIX.4.4\x01" "9=112\x01" "35=D\x01" "34=215\x01" "49=CLIENT12\x01"
					 "52=20100225-19:41:57.316\x01" "56=B\x01" "1=Marcel\x01" "11=13346\x01"
					 "21=1\x01" "40=2\x01" "44=5\x01" "54=1\x01" "59=0\x01" "60=20100225-19:39:52.020\x01"
					 "10=072\x01"),	// wrong body length
simple_message_bin = LIT("8=FIX.4.4\x01" "9=146\x01" "35=D\x01" "34=215\x01" "49=CLIENT12\x01"
						 "52=20100225-19:41:57.316\x01" "56=B\x01" "1=Marcel\x01" "11=13346\x01"
						 "21=1\x01" "40=2\x01" "44=5\x01" "54=1\x01" "59=0\x01" "60=20100225-19:39:52.020\x01"
						 "212=12\x01" "213=<blah-blah/>\x01" "10=092\x01");

#define TAG_INFO(index, type)	(((index) << 2) | (type))

#define REGULAR_TAG(index)	\
	return TAG_INFO((index), TAG_STRING)

#define BIN_TAG(index, len_tag)	\
	return TAG_INFO((index), TAG_BINARY);	\
	case (len_tag): return TAG_INFO((len_tag), TAG_LENGTH)

#define GROUP_TAG(index)	\
	return TAG_INFO((index), TAG_GROUP)

#define UNKNOWN_TAG()	return NONE

static
const fix_group_info* empty_group_info(unsigned tag UNUSED)
{
	return NULL;
}

// specification for simple message
static
unsigned simple_message_tag_info(unsigned tag)
{
	switch(tag)
	{
		case 34: 	REGULAR_TAG(0);
		case 49: 	REGULAR_TAG(1);
		case 52: 	REGULAR_TAG(2);
		case 56: 	REGULAR_TAG(3);
		case 1: 	REGULAR_TAG(4);
		case 11: 	REGULAR_TAG(5);
		case 21: 	REGULAR_TAG(6);
		case 40: 	REGULAR_TAG(7);
		case 44: 	REGULAR_TAG(8);
		case 54: 	REGULAR_TAG(9);
		case 59: 	REGULAR_TAG(10);
		case 60: 	REGULAR_TAG(11);
		default: 	UNKNOWN_TAG();
	}
}

const fix_message_info* simple_message_parser_table(const fix_string type)
{
	if(fix_string_length(type) == 1 && *type.begin == 'D')
	{
		static const fix_message_info mi = { { 12, 0, simple_message_tag_info, empty_group_info }, 0 };

		return &mi;
	}

	return NULL;
}

// specification for simple message with one tag missing
static
unsigned missing_simple_message_tag_info(unsigned tag)
{
	return tag != 11 ? simple_message_tag_info(tag) : NONE;
}

const fix_message_info* missing_tag_parser_table(const fix_string type)
{
	if(fix_string_length(type) == 1 && *type.begin == 'D')
	{
		static const fix_message_info mi = { { 12, 0, missing_simple_message_tag_info, empty_group_info }, 0 };

		return &mi;
	}

	return NULL;
}

// message with groups
const fix_string
message_with_groups = LIT("8=FIX.4.2\x01" "9=196\x01" "35=X\x01" "49=A\x01" "56=B\x01" "34=12\x01" "52=20100318-03:21:11.364\x01" "262=A\x01" "268=2\x01"
						  "279=0\x01" "269=0\x01" "278=BID\x01" "55=EUR/USD\x01" "270=1.37215\x01" "15=EUR\x01" "271=2500000\x01" "346=1\x01"
						  "279=0\x01" "269=1\x01" "278=OFFER\x01" "55=EUR/USD\x01" "270=1.37224\x01" "15=EUR\x01" "271=2503200\x01" "346=1\x01"
						  "10=171\x01"),
message_with_groups_4_4 = LIT("8=FIX.4.4\x01" "9=196\x01" "35=X\x01" "49=A\x01" "56=B\x01" "34=12\x01" "52=20100318-03:21:11.364\x01" "262=A\x01" "268=2\x01"
							  "279=0\x01" "269=0\x01" "278=BID\x01" "55=EUR/USD\x01" "270=1.37215\x01" "15=EUR\x01" "271=2500000\x01" "346=1\x01"
							  "279=0\x01" "269=1\x01" "278=OFFER\x01" "55=EUR/USD\x01" "270=1.37224\x01" "15=EUR\x01" "271=2503200\x01" "346=1\x01"
							  "10=173\x01"),
bad_message_with_groups_4_4 = LIT("8=FIX.4.4\x01" "9=196\x01" "35=X\x01" "49=A\x01" "56=B\x01" "34=12\x01" "52=20100318-03;21:11.364\x01" "262=A\x01" "268=2\x01"
							  "279=0\x01" "269=0\x01" "278=BID\x01" "55=EUR/USD\x01" "270=1.37215\x01" "15=EUR\x01" "271=2500000\x01" "346=1\x01"
							  "279=0\x01" "269=1\x01" "278=OFFER\x01" "55=EUR/USD\x01" "270=1.37224\x01" "15=EUR\x01" "271=2503200\x01" "346=1\x01"
							  "10=174\x01"),	// invalid timestamp in tag 52
bad_message_with_groups = LIT("8=FIX.4.4\x01" "9=196\x01" "35=X\x01" "49=A\x01" "56=B\x01" "34=12\x01" "52=20100318-03:21:11.364\x01" "262=A\x01" "268=2\x01"
							  "279=0\x01" "269=0\x01" "278=BID\x01" "55=EUR/USD\x01" "270=1.37215\x01" "15=EUR\x01" "271=2500000\x01" "346=1\x01"
							  "269=1\x01" "279=0\x01" "278=OFFER\x01" "55=EUR/USD\x01" "270=1.37224\x01" "15=EUR\x01" "271=2503200\x01" "346=1\x01"
							  "10=173\x01");	// invalid tag '269' on the third line above

static
unsigned message_with_groups_group_1_tag_info(unsigned tag)
{
	switch(tag)
	{
		case 279:	REGULAR_TAG(0);
		case 269: 	REGULAR_TAG(1);
		case 278: 	REGULAR_TAG(2);
		case 55: 	REGULAR_TAG(3);
		case 270: 	REGULAR_TAG(4);
		case 15: 	REGULAR_TAG(5);
		case 271: 	REGULAR_TAG(6);
		case 346: 	REGULAR_TAG(7);
		default: 	UNKNOWN_TAG();
	}
}

static
unsigned message_with_groups_root_tag_info(unsigned tag)
{
	switch(tag)
	{
		case 49: 	REGULAR_TAG(0);
		case 56: 	REGULAR_TAG(1);
		case 34: 	REGULAR_TAG(2);
		case 52: 	REGULAR_TAG(3);
		case 262: 	REGULAR_TAG(4);
		case 268: 	GROUP_TAG(5);
		default: 	UNKNOWN_TAG();
	}
}

static
const fix_group_info* message_with_groups_group_info(unsigned tag)
{
	static const fix_group_info group_1_spec = { 8, 279, message_with_groups_group_1_tag_info, empty_group_info };

	return tag == 268 ? &group_1_spec : NULL;
}

const fix_message_info* message_with_groups_parser_table(const fix_string type)
{
	if(fix_string_length(type) == 1 && *type.begin == 'X')
	{
		static const fix_message_info mi = { { 6, 0, message_with_groups_root_tag_info, message_with_groups_group_info }, 0 };

		return &mi;
	}

	return NULL;
}

// message validators -----------------------------------------------------------------------------------
bool valid_simple_message(fix_group* const group)
{
	/* 	"35=D\x01" "34=215\x01" "49=CLIENT12\x01"
		"52=20100225-19:41:57.316\x01" "56=B\x01" "1=Marcel\x01" "11=13346\x01"
		"21=1\x01" "40=2\x01" "44=5\x01" "54=1\x01" "59=0\x01" "60=20100225-19:39:52.020 */

	return valid_long(group, 		34, 	215)
		&& valid_string(group, 		49, 	CONST_LIT("CLIENT12"))
		&& valid_timestamp(group, 	52, 	&(utc_timestamp){ 2010, 2, 25, 19, 41, 57, 316 })
		&& valid_char(group, 		56, 	'B')
		&& valid_string(group, 		1, 		CONST_LIT("Marcel"))
		&& valid_long(group, 		11, 	13346)
		&& valid_long(group, 		21, 	1)
		&& valid_long(group, 		40, 	2)
		&& valid_long(group, 		44, 	5)
		&& valid_long(group, 		54, 	1)
		&& valid_long(group, 		59, 	0)
		&& valid_timestamp(group, 	60, 	&(utc_timestamp){ 2010, 2, 25, 19, 39, 52, 20 });
}

bool valid_message_with_groups(fix_group* const group)
{
	/* 	"35=X\x01" "49=A\x01" "56=B\x01" "34=12\x01" "52=20100318-03:21:11.364\x01" "262=A\x01" "268=2\x01"
		"279=0\x01" "269=0\x01" "278=BID\x01" "55=EUR/USD\x01" "270=1.37215\x01" "15=EUR\x01" "271=2500000\x01" "346=1\x01"
		"279=0\x01" "269=1\x01" "278=OFFER\x01" "55=EUR/USD\x01" "270=1.37224\x01" "15=EUR\x01" "271=2503200\x01" "346=1\x01"
		"10=171\x01" */

	bool ret = valid_char(group, 		49,		'A')
			&& valid_char(group, 		56,		'B')
			&& valid_long(group,		34,		12)
			&& valid_timestamp(group,	52,		&(utc_timestamp){ 2010, 3, 18, 3, 21, 11, 364 })
			&& valid_char(group,		262,	'A');

	if(!ret)
		return false;

	fix_group* g = NULL;
	fix_error err = get_fix_tag_as_group(group, 268, &g);

	ENSURE(err == FE_OK, "Error %d while getting group pointer from tag 268", (int)err);
	ENSURE(g, "Null child group pointer");

	const unsigned num_nodes = get_fix_group_size(g);

	ENSURE(num_nodes == 2, "Invalid number of nodes: %u", num_nodes);

	ret =  valid_long(g,	279,	0)
		&& valid_long(g,	269,	0)
		&& valid_string(g,	278,	CONST_LIT("BID"))
		&& valid_string(g,	55,		CONST_LIT("EUR/USD"))
		&& valid_double(g,	270,	1.37215)
		&& valid_string(g,	15,		CONST_LIT("EUR"))
		&& valid_long(g,	271,	2500000)
		&& valid_long(g,	346,	1);

	if(!ret)
		return false;

	ENSURE(has_more_fix_nodes(g), "Failed to scroll to the next group node");

	ret =  valid_long(g,	279,	0)
		&& valid_long(g,	269,	1)
		&& valid_string(g,	278,	CONST_LIT("OFFER"))
		&& valid_string(g,	55,		CONST_LIT("EUR/USD"))
		&& valid_double(g,	270,	1.37224)
		&& valid_string(g,	15,		CONST_LIT("EUR"))
		&& valid_long(g,	271,	2503200)
		&& valid_long(g,	346,	1);

	if(!ret)
		return false;

	ENSURE(!has_more_fix_nodes(g), "Unexpected next group node");
	return true;
}
