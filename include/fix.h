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

#include <stddef.h>
#include <stdbool.h>
#include <sys/time.h>	// struct timeval

#ifdef __cplusplus
extern "C"
{
#endif

// configuration ----------------------------------------------------------------------------------
#define MAX_MESSAGE_LENGTH	100000
#define MAX_GROUP_SIZE		1000

// gcc-specific attributes ------------------------------------------------------------------------
#ifdef __GNUC__
#define PURE_FUNC	__attribute__((__pure__))
#define NOINLINE	__attribute__((__noinline__))
#else
#define PURE_FUNC
#define NOINLINE
#endif

// string wrapper ---------------------------------------------------------------------------------
typedef struct { const char *begin, *end; } fix_string;

static inline
bool fix_string_is_empty(const fix_string s) { return !s.begin || s.begin >= s.end; }

static inline
size_t fix_string_length(const fix_string s) { return !fix_string_is_empty(s) ? (s.end - s.begin) : 0; }

fix_string fix_string_from_c_string(const char* s);
bool fix_strings_equal(const fix_string s1, const fix_string s2) PURE_FUNC;

#define CONST_LIT(s)	(const fix_string){ s "", &s[sizeof(s) - 1] }

// FIX data structures ----------------------------------------------------------------------------
#define SOH	((char)1)

// FIX message group handle
typedef struct fix_group fix_group;

// FIX parser handle
typedef struct fix_parser fix_parser;

// parser error codes
typedef enum
{
	FE_OK = -1,

	// from 'FIX Transport 1.1' document:
	FE_INVALID_TAG = 0,				// 0 = Invalid tag number
	FE_REQUIRED_TAG_MISSING = 1,	// 1 = Required tag missing
	FE_UNEXPECTED_TAG = 2,			// 2 = Tag not defined for this message type
	FE_UNDEFINED_TAG = 3,			// 3 = Undefined tag
	FE_EMPTY_VALUE = 4,				// 4 = Tag specified without a value
	FE_INVALID_VALUE = 5,			// 5 = Value is incorrect (out of range) for this tag
	FE_INCORRECT_VALUE_FORMAT = 6,	// 6 = Incorrect data format for value
	FE_DECRYPTION_PROBLEM = 7,		// 7 = Decryption problem [not used]
	FE_SIGNATURE_PROBLEM = 8,		// 8 = Signature problem [not used]
	FE_COMP_ID_PROBLEM = 9,			// 9 = CompID problem [not used]
	FE_SENDING_TIME_PROBLEM = 10,	// 10 = SendingTime accuracy problem [not used]
	FE_INVALID_MESSAGE_TYPE = 11,	// 11 = Invalid MsgType
	FE_INVALID_XML = 12,			// 12 = XML Validation error [not used]
	FE_DUPLICATE_TAG = 13,			// 13 = Tag appears more than once
	FE_INVALID_TAG_ORDER = 14,		// 14 = Tag specified out of required order
	FE_INVALID_GROUP_ORDER = 15,	// 15 = Repeating group fields out of order [not used]
	FE_INVALID_GROUP_COUNT = 16,	// 16 = Incorrect NumInGroup count for repeating group
	FE_UNEXPECTED_SOH = 17,			// 17 = Non “data” value includes field delimiter (SOH character) [not used]
	FE_OTHER = 99,					// 99 = Other

	// Fatal errors
	FE_INVALID_BEGIN_STRING,
	FE_INVALID_MESSAGE_LENGTH,
	FE_INVALID_TRAILER,
	FE_INVALID_PARSER_STATE,
	FE_OUT_OF_MEMORY
} fix_error;

typedef struct
{
	fix_error code;
	unsigned tag;
	fix_string context, msg_type;
} fix_error_details;

typedef struct
{
	fix_error_details error;
	int msg_type_code;
	fix_group* root;
} fix_parser_result;

// Parser control table structures ---------------------------------------------------------------
typedef enum { TAG_STRING, TAG_LENGTH, TAG_BINARY, TAG_GROUP } tag_value_type;

typedef struct fix_group_info
{
	unsigned node_size, first_tag;		// number of tags and the first tag in the group node
	unsigned (*get_tag_info)(unsigned);	// tag info function
	const struct fix_group_info* (*get_group_info)(unsigned);	// group function
} fix_group_info;

// parser table function return type
typedef struct
{
	fix_group_info root;
	int message_type;
} fix_message_info;

// FIX parser ------------------------------------------------------------------------------------
// constructor
fix_parser* create_fix_parser(const fix_message_info* (*parser_table)(const fix_string),
							  const fix_string fix_version);

// destructor
void free_fix_parser(fix_parser* const parser);

// message iteration
const fix_parser_result* get_first_fix_message(fix_parser* const parser, const void* bytes, unsigned num_bytes);
const fix_parser_result* get_next_fix_message(fix_parser* const parser);

// parser error
const fix_error_details* get_fix_parser_error_details(const fix_parser* const parser) PURE_FUNC;

// helpers
fix_string get_raw_fix_message(const fix_parser* const parser) PURE_FUNC;

// FIX group -------------------------------------------------------------------------------------
// group node iterator
// Intended use:
//	do { ... process group node ... } while(has_more_fix_nodes(my_group));
bool has_more_fix_nodes(fix_group* const group);

// group node iterator reset
void reset_fix_group_iterator(fix_group* const group);

// group size
unsigned get_fix_group_size(const fix_group* const group) PURE_FUNC;

// FIX group error
const fix_error_details* get_fix_group_error_details(const fix_group* const group) PURE_FUNC;

// tag access -------------------------------------------------------------------------------------
// tag as string
fix_error get_fix_tag_as_string(const fix_group* const group, unsigned tag, fix_string* const result);

// copy tag as string
fix_error copy_fix_tag_as_string(const fix_group* const group, unsigned tag, char** const result);

// tag as group
fix_error get_fix_tag_as_group(const fix_group* const group, unsigned tag, fix_group** const result);

// tag as single char
fix_error get_fix_tag_as_char(const fix_group* const group, unsigned tag, char* const result);

// tag as long integer
fix_error get_fix_tag_as_long(const fix_group* const group, unsigned tag, long* const result);

// tag as double
fix_error get_fix_tag_as_double(const fix_group* const group, unsigned tag, double* const result);

// tag as boolean
fix_error get_fix_tag_as_boolean(const fix_group* const group, unsigned tag, bool* const result);

// tag as utc_timestamp
typedef struct
{
	unsigned short year;
	unsigned char month, day, hour, minute, second;
	unsigned short millisecond;
} utc_timestamp;

fix_error get_fix_tag_as_utc_timestamp(const fix_group* const group, unsigned tag, utc_timestamp* const result);

// tag as tz_timestamp
typedef struct
{
	utc_timestamp utc;
	short offset_minutes;
} tz_timestamp;

fix_error get_fix_tag_as_tz_timestamp(const fix_group* const group, unsigned tag, tz_timestamp* const result);

// tag as LocalMktDate
fix_error get_fix_tag_as_LocalMktDate(const fix_group* const group, unsigned tag, utc_timestamp* const result);

// tag as FIX version (for ApplVerID, DefaultApplVerID and RefApplVerID)
typedef enum
{
	FIX27,
	FIX30,
	FIX40,
	FIX41,
	FIX42,
	FIX43,
	FIX44,
	FIX50,
	FIX50SP1,
	FIX50SP2
} fix_version;

fix_error get_fix_tag_as_fix_version(const fix_group* const group, unsigned tag, fix_version* const result);

// generic get_fix_tag()
// supported from gcc 4.9 only :(
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 9)
#define get_fix_tag(g, t, p)	_Generic((p),	\
	fix_string*		: get_fix_tag_as_string,	\
	long*			: get_fix_tag_as_long,		\
	double*			: get_fix_tag_as_double,	\
	bool*			: get_fix_tag_as_boolean,	\
	char*			: get_fix_tag_as_char,	\
	fix_group**		: get_fix_tag_as_group		\
	fix_version*	: get_fix_tag_as_fix_version	\
	)((g), (t), (p))
#endif

// utilities -------------------------------------------------------------------------------------
// utc_timestamp to struct timeval converter
fix_error utc_timestamp_to_timeval(const utc_timestamp* const utc, struct timeval* const result);

// error message string from fix_error_details
const char* compose_fix_error_message(const fix_error_details* const details) NOINLINE;

// fix_error to string
const char* fix_error_to_string(fix_error code) PURE_FUNC;

// FIX message type string to type code converter (for RefMsgType)
int fix_message_type_to_code(const fix_parser* const parser, const fix_string s) PURE_FUNC;

#ifdef __cplusplus
}
#endif
