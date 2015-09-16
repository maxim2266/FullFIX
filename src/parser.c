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
#include <errno.h>

// groups cleanup
static
void free_groups(fix_group* group)
{
	while(group)
	{
		fix_group* const next = group->next_gc;

		free(group);
		group = next;
	}
}

// group allocator
static
fix_group* alloc_group(fix_parser* const parser, const fix_group_info* const ginfo, unsigned num_nodes)
{
	// allocate memory
	const unsigned n = num_nodes * ginfo->node_size * sizeof(tag_value);
	fix_group* const group = malloc(sizeof(fix_group) + n);

	if(!group)
	{
		set_fatal_error(parser, FE_OUT_OF_MEMORY);
		return NULL;
	}

	// set-up
	*group = (fix_group){ ginfo, &parser->result.error, parser->result.root->next_gc, num_nodes, 0 };
	parser->result.root->next_gc = group;

	// clear tag values
	memset(group->tags, 0, n);

	// all done
	return group;
}

// root group reallocation
static
bool prepare_root_group(fix_parser* const parser, const fix_group_info* const info)
{
	fix_parser_result* const result = &parser->result;

	// clear child groups, if any
	if(result->root)
	{
		free_groups(result->root->next_gc);
		result->root->next_gc = NULL;
	}

	// required number of tags
	const unsigned n = info ? info->node_size : INITIAL_NUM_TAGS;

	if(n > parser->root_capacity)
	{
		fix_group* const group = realloc(result->root, sizeof(fix_group) + n * sizeof(tag_value));

		if(!group)
		{
			set_fatal_error(parser, FE_OUT_OF_MEMORY);
			return false;
		}

		// update parser
		result->root = group;
		parser->root_capacity = n;
	}

	// set-up
	*result->root = (fix_group){ info, &result->error, NULL, 1, 0 };

	// clear tag values
	memset(&result->root->tags, 0, n * sizeof(tag_value));
	return true;
}

// read FIX uint
static
unsigned read_uint(const char* s, const char** end)
{
	unsigned r;
	int c = CHAR_TO_INT(*s);

	switch(c)
	{
		case '1' ... '9':
			r = c - '0';
			break;
		default:
			*end = s;
			return 0;	// error: invalid first digit
	}

	for(int i = 0; i < 9; ++i)	// up to 9 digits
	{
		c = CHAR_TO_INT(*++s);

		switch(c)
		{
			case '0' ... '9':
				r = r * 10 + (c - '0');
				break;
			default:
				*end = s;
				return r;	// conversion complete
		}
	}

	*end = s;
	return 0;	// error: more than 9 digits
}

// read next tag code
// also sets up the context and checks for non-empty value
// returns 0 on error or end of input
static
unsigned next_tag(fix_parser* const parser)
{
	fix_error_details* const details = &parser->result.error;
	const char* s = parser->frame.begin;

	if(s >= parser->frame.end)	// end of input
	{
		set_error_ctx(details, FE_OK, 0, EMPTY_STR);
		return 0;
	}

	// read tag
	const char* e;
	const unsigned tag = details->tag = read_uint(s, &e);
	const char b = *e++;

	details->context = (fix_string){ s, e };

	// check the tag
	if(b != '=' || tag == 0)
	{
		details->code = FE_INVALID_TAG;
		return 0;
	}

	if(e >= parser->frame.end)	// empty tag value
	{
		details->code = FE_EMPTY_VALUE;
		return 0;
	}

	// OK
	parser->frame.begin = e;	// advance frame
	details->code = FE_OK;		// clear error
	return tag;
}

// match the next tag
// returns 'true' on exact match or 'false' on error
static
bool match_next_tag(fix_parser* const parser, unsigned tag)
{
	const unsigned t = next_tag(parser);

	if(t == tag)
		return true;

	// match errors
	fix_error_details* const details = &parser->result.error;

	if(t != 0)	// some other tag
		set_error(details, FE_INVALID_TAG_ORDER, tag);	// 'tag' is the expected one
	else if(details->code == FE_OK)	// end of input
		set_error(details, FE_REQUIRED_TAG_MISSING, tag);

	return false;
}

// read tag value as unsigned integer
static
unsigned read_uint_value(fix_parser* const parser)
{
	const char* end;
	const unsigned val = read_uint(parser->frame.begin, &end);

	// set context

	// check the value
	if(*end != SOH)
	{
		parser->result.error.code = FE_INCORRECT_VALUE_FORMAT;
		parser->result.error.context.end = end + 1;
		return 0;
	}

	// OK
	parser->result.error.code = FE_OK;
	parser->result.error.context.end = end;
	parser->frame.begin = end + 1;	// advance frame
	return val;
}

// read bytes to the first SOH, i.e., a FIX string
// never fails
static inline
fix_string read_string(fix_parser* const parser)
{
	const fix_string res = { parser->frame.begin, rawmemchr(parser->frame.begin, SOH) };

	parser->frame.begin = res.end + 1;
	return res;
}

// helpers for retrieving the address of the tag value structure
static
tag_value* tag_value_checked_ptr(fix_group* const group, unsigned tag_info)
{
	tag_value* const ptr = &group->tags[group->node_base + TAG_INDEX(tag_info)];

	if(!ptr->group)	// check for duplicate
		return ptr;

	group->error->code = FE_DUPLICATE_TAG;
	return NULL;
}

static
tag_value* binary_tag_value_checked_ptr(fix_group* const group, unsigned len_tag_info)
{
	const unsigned ti = group->info->get_tag_info(TAG_MAIN(len_tag_info));

	if(ti != NONE && TAG_TYPE(ti) == TAG_BINARY)
		return tag_value_checked_ptr(group, ti);

	// invalid parser spec., must never happen
	group->error->code = FE_INVALID_PARSER_STATE;
	return NULL;
}

// checked group info
static
const fix_group_info* safe_group_info(const fix_group* const group, unsigned tag)
{
	const fix_group_info* const info = group->info->get_group_info(tag);

	if(info)
		return info;

	group->error->code = FE_INVALID_PARSER_STATE;	// invalid spec., must never happen
	return NULL;
}

// read tag string and the next tag
static
void read_string_and_get_next(fix_parser* const parser, tag_value* const result)
{
	if(result)
	{
		result->value = read_string(parser);
		next_tag(parser);
	}
}

// read binary value and the next tag
static
void read_binary_and_get_next(fix_parser* const parser, const unsigned bin_tag, tag_value* const result)
{
	if(!result)
		return;

	// read length value
	const unsigned len = read_uint_value(parser);
	fix_error_details* const details = &parser->result.error;

	if(details->code != FE_OK)
		return;	// something has gone wrong

	if(len == 0)	// nothing to do, just proceed to the next tag
	{
		next_tag(parser);
		return;
	}

	// save length tag and context for error reporting
	const unsigned len_tag = details->tag;
	const fix_string len_ctx = details->context;

	// match binary tag
	if(!match_next_tag(parser, bin_tag))
		return;

	// get and check the binary string
	const fix_string res = { parser->frame.begin, parser->frame.begin + len };

	if(res.end > parser->frame.end || *res.end != SOH)
	{
		set_error_ctx(details, FE_INVALID_VALUE, len_tag, len_ctx);
		return;
	}

	// store the string
	result->value = res;
	parser->frame.begin = res.end + 1;

	// next tag
	next_tag(parser);
}

// forward declaration
static
void read_group_and_get_next(fix_parser* const parser, const fix_group_info* const info, tag_value* const result);

// tag processor
// returns false on error or unknown tag
static
bool process_tag_and_get_next(fix_parser* const parser, fix_group* const group)
{
	const unsigned 	tag = parser->result.error.tag,
					ti = group->info->get_tag_info(tag);

	if(ti == NONE)
		return false;	// maybe the tag is not from this group

	switch(TAG_TYPE(ti))
	{
		case TAG_STRING:
			read_string_and_get_next(parser, tag_value_checked_ptr(group, ti));
			break;
		case TAG_LENGTH:
			read_binary_and_get_next(parser, TAG_MAIN(ti), binary_tag_value_checked_ptr(group, ti));
			break;
		case TAG_BINARY:
			parser->result.error.code = FE_INVALID_TAG_ORDER;
			return false;
		case TAG_GROUP:
			read_group_and_get_next(parser,
									safe_group_info(group, tag),
									tag_value_checked_ptr(group, ti));
			break;
	}

	return parser->result.error.code == FE_OK;
}

// group reader
static
void read_group_and_get_next(fix_parser* const parser, const fix_group_info* const info, tag_value* const result)
{
	if(!info || !result)
		return;

	// read number of nodes
	const unsigned len = read_uint_value(parser);

	if(parser->result.error.code != FE_OK)
		return;	// something has gone wrong

	if(len == 0)	// nothing to do, just proceed to the next tag
	{
		next_tag(parser);
		return;
	}

	if(len > MAX_GROUP_SIZE)
	{
		parser->result.error.code = FE_INVALID_VALUE;
		return;
	}

	// new group
	fix_group* const group = alloc_group(parser, info, len);

	if(!group)
		return;

	// save length tag and context for error reporting
	const unsigned len_tag = group->error->tag;
	const fix_string len_ctx = group->error->context;

	// match first tag in group
	if(!match_next_tag(parser, info->first_tag))
		return;

	// read the group
	while(process_tag_and_get_next(parser, group))
	{
		if(group->error->tag == info->first_tag)	// starting new group node
		{
			const unsigned n = info->node_size;

			group->node_base += n;

			if(group->node_base >= n * len)	// too many nodes
			{
				set_error_ctx(group->error, FE_INVALID_GROUP_COUNT, len_tag, len_ctx);
				break;
			}
		}
	}

	// reset node iterator
	group->node_base = 0;

	// store the group
	result->group = group;
}

// parser entry point
static
const fix_parser_result* run(fix_parser* const parser)
{
	// scanner
	if(!extract_next_message(parser))
		return NULL;

	// check message result and begin string
	fix_parser_result* const result = &parser->result;

	if(result->error.code != FE_OK)
		return result;

	// message type
	if(!match_next_tag(parser, 35))
		return result;

	const fix_string mt = result->error.msg_type = read_string(parser);

	// message info
	const fix_message_info* const pmi = parser->parser_table(mt);

	if(!pmi)
	{
		set_error_ctx(&result->error, FE_INVALID_MESSAGE_TYPE, 35, mt);
		return result;
	}

	// store message type code
	result->msg_type_code = pmi->message_type;

	// set-up root group
	if(!prepare_root_group(parser, &pmi->root))
		return NULL;

	// read the rest
	if(next_tag(parser) != 0)
		while(process_tag_and_get_next(parser, result->root));

	// check for errors
	if(result->error.code > FE_OTHER)	// fatal error
		return NULL;

	if(result->error.code == FE_OK && result->error.tag != 0)
		result->error.code = FE_UNEXPECTED_TAG;

	// all done
	return result;
}

// parser check
static inline
bool is_usable_parser(const fix_parser* const parser)
{
	return parser && parser->result.error.code <= FE_OTHER;
}

// parser interface implementation -------------------------------------------------------------------------------
// constructor
fix_parser* create_fix_parser(const fix_message_info* (*parser_table)(const fix_string),
							  const fix_string fix_version)
{
	if(!parser_table
		|| fix_string_length(fix_version) < sizeof("FIX.4.4") - 1
		|| fix_string_length(fix_version) > sizeof("FIXT.1.1") - 1
		|| fix_version.begin[0] != 'F' || fix_version.begin[1] != 'I' || fix_version.begin[2] != 'X' || fix_version.begin[3] != '.')
	{
		errno = EINVAL;
		return NULL;
	}

	fix_parser* const parser = NEW(fix_parser);

	if(!parser)
		return NULL;

	// initialise scanner and root
	if(!init_scanner(parser) || !prepare_root_group(parser, NULL))
	{
		free_fix_parser(parser);
		errno = ENOMEM;
		return NULL;
	}

	// parser table
	parser->parser_table = parser_table;

	// FIX begin string (e.g. "8=FIXT.1.1|9=")
	char* p = parser->header;

	*p++ = '8';
	*p++ = '=';

	unsigned char checksum = '8' + '=' + SOH + '9' + '=';

	for(const char* s = fix_version.begin; s < fix_version.end; )
		checksum += (*p++ = *s++);

	parser->header_checksum = checksum;
	*p++ = SOH;
	*p++ = '9';
	*p++ = '=';
	parser->header_len = p - parser->header;

	// error code
	parser->result.error.code = FE_OK;
	return parser;
}

// destructor
void free_fix_parser(fix_parser* const parser)
{
	if(parser)
	{
		if(parser->body)
			free(parser->body);

		free_groups(parser->result.root);
		free(parser);
	}
}

// message iterators
const fix_parser_result* get_first_fix_message(fix_parser* const parser, const void* bytes, unsigned num_bytes)
{
	if(!is_usable_parser(parser))
		return NULL;

	if(parser->state.src != parser->state.end)	// unprocessed input
	{
		set_fatal_error(parser, FE_INVALID_PARSER_STATE);
		return NULL;
	}

	// store pointers
	parser->state.src = bytes;
	parser->state.end = bytes + num_bytes;

	// run the parser
	return run(parser);
}

const fix_parser_result* get_next_fix_message(fix_parser* const parser)
{
	return is_usable_parser(parser) ? run(parser) : NULL;
}

// raw message access
fix_string get_raw_fix_message(const fix_parser* parser)
{
	return is_usable_parser(parser) && parser->body_length > 0
			? (fix_string){ parser->body, parser->body + parser->body_length }
			: EMPTY_STR;
}

// parser error
const fix_error_details* get_fix_parser_error_details(const fix_parser* const parser)
{
	return parser ? &parser->result.error : NULL;
}

// group API implementation -------------------------------------------------------------------------------
// group node iterator
bool has_more_fix_nodes(fix_group* const group)
{
	if(group && group->node_base != NONE)
	{
		const unsigned node_size = group->info->node_size;

		group->node_base += node_size;

		if(group->node_base < group->num_nodes * node_size)
			return true;

		group->node_base = NONE;
	}

	return false;
}

// group node iterator reset
void reset_fix_group_iterator(fix_group* const group)
{
	if(group)
		group->node_base = 0;
}

// group size
unsigned get_fix_group_size(const fix_group* const group)
{
	return group ? group->num_nodes : 0;
}

const fix_error_details* get_fix_group_error_details(const fix_group* const group)
{
	return group ? group->error : NULL;
}

// tag accessors ------------------------------------------------------------------------------------------
static
fix_error set_group_error(const fix_group* const group, unsigned tag, fix_error err)
{
	set_error_ctx(group->error, err, tag, EMPTY_STR);
	return err;
}

// tag as string
fix_error get_fix_tag_as_string(const fix_group* const group, unsigned tag, fix_string* const result)
{
	if(!group || group->node_base == NONE)
		return FE_OTHER;

	const unsigned ti = group->info->get_tag_info(tag);

	if(ti == NONE)
		return set_group_error(group, tag, FE_UNEXPECTED_TAG);

	switch(TAG_TYPE(ti))
	{
		case TAG_STRING:
		case TAG_BINARY:
			break;
		case TAG_LENGTH:
			return set_group_error(group, tag, FE_UNEXPECTED_TAG);
		case TAG_GROUP:
			return set_group_error(group, tag, FE_INCORRECT_VALUE_FORMAT);
	}

	const fix_string value = group->tags[group->node_base + TAG_INDEX(ti)].value;

	if(fix_string_is_empty(value))
		return set_group_error(group, tag, FE_REQUIRED_TAG_MISSING);

	if(result)
		*result = value;

	set_error_ctx(group->error, FE_OK, tag, value);
	return FE_OK;
}

// tag as group
fix_error get_fix_tag_as_group(const fix_group* const group, unsigned tag, fix_group** const result)
{
	if(!group || group->node_base == NONE)
		return FE_OTHER;

	const unsigned ti = group->info->get_tag_info(tag);

	if(ti == NONE)
		return set_group_error(group, tag, FE_UNEXPECTED_TAG);

	switch(TAG_TYPE(ti))
	{
		case TAG_GROUP:
			break;
		case TAG_STRING:
		case TAG_BINARY:
			return set_group_error(group, tag, FE_INCORRECT_VALUE_FORMAT);
		case TAG_LENGTH:
			return set_group_error(group, tag, FE_UNEXPECTED_TAG);
	}

	fix_group* const g = group->tags[group->node_base + TAG_INDEX(ti)].group;

	if(!g)
		return set_group_error(group, tag, FE_REQUIRED_TAG_MISSING);

	if(result)
		*result = g;

	set_error_ctx(group->error, FE_OK, tag, EMPTY_STR);
	return FE_OK;
}
