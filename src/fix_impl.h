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

#include "fix.h"
#include <memory.h>
#include <malloc.h>

// helper macros
#define CHAR_TO_INT(c) ((int)(unsigned char)(c))
#define NONE ((unsigned)-1)

// literals
#define LIT(s)		{ s "", s + sizeof(s) - 1 }
#define EMPTY_STR	(const fix_string){ NULL, NULL }

// allocator
#define NEW(T)	calloc(1, sizeof(T))

// tag info fields access
#define TAG_TYPE(t)		((tag_value_type)((t) & 3))
#define TAG_INDEX(t)	((t) >> 2)
#define TAG_MAIN		TAG_INDEX

// scanner state
typedef struct
{
	const char *src, *end;
	char* dest;
	unsigned counter;
	int label;
	unsigned char check_sum;
} scanner_state;

// parser
struct fix_parser
{
	// error data
	fix_parser_result result;

	// scanner state
	scanner_state state;

	// raw message frame
	fix_string frame;

	// raw message buffer
	char* body;
	unsigned body_length, body_capacity;

	// root group capacity
	unsigned root_capacity;	// max number of tag_value's

	// parser settings
	const fix_message_info* (*parser_table)(const fix_string);

	// FIX message header
	char header[sizeof("8=FIXT.1.1|9=") - 1];
	unsigned header_len;
	unsigned char header_checksum;
};

// parser configuration
#define INITIAL_BODY_SIZE	200
#define INITIAL_NUM_TAGS	20

// fix tag value
typedef union
{
	fix_group* group;
	fix_string value;
} tag_value;

// fix group node
struct fix_group
{
	const fix_group_info* info;		// group info
	fix_error_details* error;		// error details pointer
	fix_group* next_gc;				// gc chain
	unsigned num_nodes, node_base;	// number of nodes, iterator
	tag_value tags[];				// tag space
};

// scanner
bool init_scanner(fix_parser* parser);
bool extract_next_message(fix_parser* const parser) __attribute__((nonnull));

// utils -----------------------------------------------------------------------------
// error setters
void set_fatal_error(fix_parser* const parser, fix_error code) __attribute__((nonnull));

static inline
void set_error(fix_error_details* const details, fix_error err, unsigned tag)
{
	details->code = err;
	details->tag = tag;
}

static inline
void set_error_ctx(fix_error_details* const details, fix_error err, unsigned tag, const fix_string ctx)
{
	details->context = ctx;
	set_error(details, err, tag);
}
