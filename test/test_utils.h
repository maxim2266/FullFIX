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

#pragma once

#include <fix.h>
#include <stdio.h>

// fix_string support
#define LIT(s)			{ s "", &s[sizeof(s) - 1] }

// attributes
#define UNUSED __attribute__((__unused__))

// validators
#define REPORT_FAILURE(fmt, ...)	\
	(fprintf(stderr, "%s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__), fflush(stderr))

#define ENSURE(cond, fmt, ...)	\
	if(!(cond)) { REPORT_FAILURE(fmt, ##__VA_ARGS__); return false; } else ((void)0)

#define TEST_END(r)	printf((r) ? "Passed: %s\n" : "FAILED: %s\n", __func__); return (r)

#define PASSED printf("Passed: %s\n", __func__); return true

// utilities ----------------------------------------------------------------------------------
void* check_ptr(void* p);
fix_string make_n_copies(size_t n, const fix_string src);
fix_string make_n_copies_of_multiple_messages(size_t n, const fix_string src[], size_t n_src);
bool equal_utc_timestamps(const utc_timestamp* ts1, const utc_timestamp* ts2);
void print_times(const char* test_name, size_t num_messages,
				 const struct timespec* start, const struct timespec* stop);

// error details reporter
void report_error_details(const fix_error_details* const details, const char* file_name, int line_no);

// generic tag retriever
#define GET_TAG(g, t, v, f)	\
	if((f)((g), (t), &(v)) != FE_OK)	\
		return (report_error_details(get_fix_group_error_details(g), __FILE__, __LINE__), false)

// parser result validator
bool parser_result_ok(const fix_parser_result* const res, const char* file_name, int line_no);

#define ENSURE_PARSER_RESULT(r)	\
	if(!parser_result_ok((r), __FILE__, __LINE__)) return false

// typed validators
bool valid_string(fix_group* group, unsigned tag, const fix_string expected);
bool valid_long(fix_group* group, unsigned tag, const long expected);
bool valid_double(fix_group* group, unsigned tag, const double expected);
bool valid_char(fix_group* group, unsigned tag, const char expected);
bool valid_boolean(fix_group* group, unsigned tag, const bool expected);
bool valid_timestamp(fix_group* group, unsigned tag, const utc_timestamp* expected);

// parser invocations -------------------------------------------------------------------------
// message function
typedef bool (*message_function)(const fix_parser_result* const, const fix_string);

// call function 'f' once per each message from 'input'
bool parse_input(fix_parser* const parser, const fix_string input, message_function f);

// same as above, but free the parser afterwards
bool parse_input_once(fix_parser* const parser, const fix_string input, message_function f);

// test messages -----------------------------------------------------------------------------
extern const fix_string simple_message, bad_message_1, bad_message_2, simple_message_bin,
						message_with_groups, message_with_groups_4_4, bad_message_with_groups_4_4,
						bad_message_with_groups;

// parser table for simple message
const fix_message_info* simple_message_parser_table(const fix_string type);
const fix_message_info* missing_tag_parser_table(const fix_string type);

// parser table for message with groups
const fix_message_info* message_with_groups_parser_table(const fix_string type);

// message validators ------------------------------------------------------------------------
bool valid_simple_message(fix_group* const group);
bool valid_message_with_groups(fix_group* const group);
