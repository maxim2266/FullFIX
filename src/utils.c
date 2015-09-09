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

#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

// helpers -----------------------------------------------------------------------------------
static
int safe_length(const fix_string s)
{
	const size_t len = fix_string_length(s);

	return len < 100 ? ((int)len) : 100;
}

// API implementation ------------------------------------------------------------------------
// fix string
bool fix_strings_equal(const fix_string s1, const fix_string s2)
{
	const size_t n1 = fix_string_length(s1), n2 = fix_string_length(s2);

	return n1 == n2 && (n1 == 0 || memcmp(s1.begin, s2.begin, n1) == 0);
}

fix_string fix_string_from_c_string(const char* s)
{
	const size_t len = s ? strlen(s) : 0;

	return len > 0 ? (fix_string){ s, s + len } : EMPTY_STR;
}

// error setter
void set_fatal_error(fix_parser* const parser, fix_error code)
{
	fix_error_details* const details = &parser->result.error;

	details->code = code;
	details->tag = 0;
	details->context = EMPTY_STR;
}

// utc_timestamp to struct timespec converter
fix_error utc_timestamp_to_timeval(const utc_timestamp* const utc, struct timeval* const result)
{
	if(!utc
		|| utc->year > 9999
		|| utc->month - 1 > 11
		|| utc->day - 1 > 30
		|| utc->hour > 23
		|| utc->minute > 59
		|| utc->second > 60
		|| utc->millisecond > 999)
		return FE_INVALID_VALUE;

	// seconds
	struct timeval value;

	value.tv_sec = timegm(&(struct tm)
	{
		.tm_year = utc->year - 1900,
		.tm_mon = utc->month - 1,
		.tm_mday = utc->day,
		.tm_hour = utc->hour,
		.tm_min = utc->minute,
		.tm_sec = utc->second,
		.tm_wday = 0,
		.tm_yday = 0,
		.tm_isdst = -1
	});

	if(value.tv_sec == (time_t)-1)
		return FE_INVALID_VALUE;

	// microseconds
	value.tv_usec = utc->millisecond * 1000;

	// all done
	if(result)
		*result = value;

	return FE_OK;
}

const char* compose_fix_error_message(const fix_error_details* const details)
{
	if(!details)
	{
		errno = EINVAL;
		return NULL;
	}

	if(details->code == FE_OK)
	{
		errno = 0;
		return NULL;
	}

	int n;
	char* res;

	switch(details->code)
	{
		case FE_INVALID_PARSER_STATE:
		case FE_OUT_OF_MEMORY:
			n = asprintf(&res, "Fatal error (%d): %s", (int)details->code, fix_error_to_string(details->code));
			break;
		case FE_INVALID_BEGIN_STRING:
		case FE_INVALID_MESSAGE_LENGTH:
		case FE_INVALID_TRAILER:
			n = asprintf(&res, "Fatal error (%d): %s [Tag = %u, Context = \"%.*s\"]",
						 (int)details->code, fix_error_to_string(details->code),
						 details->tag, safe_length(details->context), details->context.begin);
			break;
		default:
			n = asprintf(&res, "Error (%d): %s [Tag = %u, MsgType = \"%.*s\", Context = \"%.*s\"]",
						(int)details->code, fix_error_to_string(details->code), details->tag,
						safe_length(details->msg_type), details->msg_type.begin,
						safe_length(details->context), details->context.begin);
			break;
	}

	return n > 0 ? res : NULL;
}

const char* fix_error_to_string(fix_error code)
{
	switch(code)
	{
		case FE_OK:
			return "No error";
		case FE_INVALID_TAG:
			return "Invalid tag number";
		case FE_REQUIRED_TAG_MISSING:
			return "Required tag missing";
		case FE_UNEXPECTED_TAG:
			return "Tag not defined for this message type";
		case FE_UNDEFINED_TAG:
			return "Undefined tag";
		case FE_EMPTY_VALUE:
			return "Tag specified without a value";
		case FE_INVALID_VALUE:
			return "Value is incorrect (out of range) for this tag";
		case FE_INCORRECT_VALUE_FORMAT:
			return "Incorrect data format for value";
		case FE_DECRYPTION_PROBLEM:
			return "Decryption problem";
		case FE_SIGNATURE_PROBLEM:
			return "Signature problem";
		case FE_COMP_ID_PROBLEM:
			return "CompID problem";
		case FE_SENDING_TIME_PROBLEM:
			return "SendingTime accuracy problem";
		case FE_INVALID_MESSAGE_TYPE:
			return "Invalid MsgType";
		case FE_INVALID_XML:
			return "XML Validation error";
		case FE_DUPLICATE_TAG:
			return "Tag appears more than once";
		case FE_INVALID_TAG_ORDER:
			return "Tag specified out of required order";
		case FE_INVALID_GROUP_ORDER:
			return "Repeating group fields out of order";
		case FE_INVALID_GROUP_COUNT:
			return "Incorrect NumInGroup count for repeating group";
		case FE_UNEXPECTED_SOH:
			return "Non \"data\" value includes field delimiter (SOH character)";
		case FE_OTHER:
			return "Other error";
		case FE_INVALID_BEGIN_STRING:
			return "Invalid begin string";
		case FE_INVALID_MESSAGE_LENGTH:
			return "Invalid message length format";
		case FE_INVALID_TRAILER:
			return "Invalid message checksum format";
		case FE_INVALID_PARSER_STATE:
			return "Invalid parser state";
		case FE_OUT_OF_MEMORY:
			return "Out of memory";
		default:
			return "Unknown error";
	}
}
