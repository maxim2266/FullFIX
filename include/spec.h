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

// macro to support compiled specification
#define NONE ((unsigned)-1)

// tag info ----------------------------------------------------------------------------------
#define TAG_INFO(index, type)	(((index) << 2) | (type))

#define REG_TAG_INFO(name, index)	\
	case name: return TAG_INFO((index), TAG_STRING);

#define BIN_TAG_INFO(name, len_value, index)	\
	case name: return TAG_INFO((index), TAG_BINARY);	\
	case (len_value): return TAG_INFO(name, TAG_LENGTH);

#define GRP_TAG_INFO(name, index)	\
	case name: return TAG_INFO((index), TAG_GROUP);

// group --------------------------------------------------------------------------------------
#define TAG_INFO_FUNC(name)	\
static unsigned name ## _tag_info_func(const unsigned tag)	\
{	\
	switch(tag)	\
	{

#define END_TAG_INFO	\
		default: return NONE;	\
	}	\
}

#define GROUP_INFO_FUNC(name)	\
static const fix_group_info* name ## _group_info_func(const unsigned tag)	\
{	\
	switch(tag)	\
	{

#define GROUP_INFO(len_tag_name, group_name)	\
	case len_tag_name: return &group_name ## _group_info;

#define END_GROUP_INFO	\
		default: return NULL;	\
	}	\
}

#define GROUP_INFO_STRUCT(name, node_size, first_tag)	\
static const fix_group_info	\
name ## _group_info = { (node_size), (first_tag), name ## _tag_info_func, name ## _group_info_func };

#define EMPTY_GROUP_INFO(name, node_size, first_tag)	\
static const fix_group_info	\
name ## _group_info = { (node_size), (first_tag), name ## _tag_info_func, empty_group_info_func };

// message ---------------------------------------------------------------------------------------
#define MESSAGE_TAG_INFO_FUNC		TAG_INFO_FUNC

#define END_MESSAGE_TAG_INFO	\
		default: return common_tag_info_func(tag);	\
	}	\
}

#define MESSAGE_GROUP_INFO_FUNC	GROUP_INFO_FUNC

#define END_MESSAGE_GROUP_INFO	\
		default: return common_group_info_func(tag);	\
	}	\
}

#define MESSAGE_GROUP_INFO_STRUCT(name, node_size)	\
static const fix_message_info	\
name ## _message_info = { { (node_size), 0, name ## _tag_info_func, name ## _group_info_func }, name };

#define EMPTY_MESSAGE_GROUP_INFO(name, node_size)	\
static const fix_message_info	\
name ## _message_info = { { (node_size), 0, name ## _tag_info_func, common_group_info_func }, name };

// parser table ----------------------------------------------------------------------------------
#define RETURN_MESSAGE(name)	\
	return &name ## _message_info

#define RETURN_MESSAGE_OR_NULL(name)	\
	return *s == SOH ? &name ## _message_info : NULL
