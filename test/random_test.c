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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <error.h>
#include <errno.h>
#include <math.h>

// error handling
#define FAIL()	error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__, "%s() failed", __func__)

#define ASSURE(cond)	if(!(cond)) FAIL()

// helpers
static
void print_fix_message(const char* msg, unsigned len, FILE* const stream)
{
	const char* const end = msg + len;

	for(const char* s = memchr(msg, SOH, len); s; s = memchr(msg, SOH, end - msg))
	{
		const unsigned n = s - msg;

		ASSURE(fwrite(msg, 1, n, stream) == n && fputc('|', stream) != EOF);
		msg = s + 1;
	}

	len = end - msg;

	if(len > 0)
		ASSURE(fwrite(msg, 1, len, stream) == len);

	ASSURE(putc('\n', stream) != EOF);
}

static
void print_raw_message(const fix_parser* const parser)
{
	const fix_string raw_msg = get_raw_fix_message(parser);

	print_fix_message(raw_msg.begin, fix_string_length(raw_msg), stderr);
}

// FIX message structures
typedef struct
{
	char MsgType;
	char SenderCompID[50], TargetCompID[50];
	long MsgSeqNum;
	utc_timestamp SendingTime;
	char PossDupFlag;
} header;

typedef struct
{
	char Account[50], ClOrdID[50];
	utc_timestamp TransactTime;
	char HandlInst, OrdType, Side, TimeInForce;
	double Price;
} new_order_single;

typedef struct
{
	header hdr;

	union
	{
		new_order_single order;
		// more to come...
	};
} fix_message_data;

// validators
static
bool valid_header(fix_group* const group, const header* const hdr)
{
	return valid_string(group, SenderCompID, fix_string_from_c_string(hdr->SenderCompID))
		&& valid_string(group, TargetCompID, fix_string_from_c_string(hdr->TargetCompID))
		&& valid_long(group, MsgSeqNum, hdr->MsgSeqNum)
		&& valid_timestamp(group, SendingTime, &hdr->SendingTime)
		&& valid_char(group, PossDupFlag, hdr->PossDupFlag);
}

static
bool valid_new_order_single(fix_group* const group, const new_order_single* const order)
{
	return valid_string(group, Account, fix_string_from_c_string(order->Account))
		&& valid_string(group, ClOrdID, fix_string_from_c_string(order->ClOrdID))
		&& valid_timestamp(group, TransactTime, &order->TransactTime)
		&& valid_char(group, HandlInst, order->HandlInst)
		&& valid_char(group, OrdType, order->OrdType)
		&& valid_char(group, Side, order->Side)
		&& valid_char(group, TimeInForce, order->TimeInForce)
		&& valid_double(group, Price, order->Price);
}

static
bool valid_fix_message(fix_group* const group, const fix_message_data* const data)
{
	const char
		type = data->hdr.MsgType,
		real_type = get_fix_group_error_details(group)->msg_type.begin[0];

	ENSURE(real_type == type, "Message type mismatch: expected '%c', got '%c'", type, real_type);

	if(!valid_header(group, &data->hdr))
		return false;

	switch(type)
	{
		case 'D':
			return valid_new_order_single(group, &data->order);
		default:
			REPORT_FAILURE("Unexpected message type '%c'", type);
			return false;
	}
}

// random generators
static
void gen_random_string(char* p, unsigned n)
{
	static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz_0123456789";

	n = 1 + (rand() % (n - 1));

	for(unsigned i = 0; i < n; ++i)
		p[i] = letters[rand() % (sizeof(letters) - 1)];

	p[n] = 0;
}

static
void gen_timestamp(utc_timestamp* ts)
{
	ts->year = 1970 + rand() % 100;
	ts->month = (rand() % 12) + 1;
	ts->day = (rand() % 30) + 1;
	ts->hour = rand() % 24;
	ts->minute = rand() % 60;
	ts->second = rand() % 60;
	ts->millisecond = rand() % 1000;
}

#define CHAR_FROM(s)	(s ""[rand() % (sizeof(s) - 1)])

static
double gen_price()
{
	return (double)(rand() % 10000) / 100. + 0.1;
}

// constructors
static
void gen_header(header* const hdr)
{
	gen_random_string(hdr->SenderCompID, sizeof(hdr->SenderCompID));
	gen_random_string(hdr->TargetCompID, sizeof(hdr->TargetCompID));
	hdr->MsgSeqNum = rand() + 1;
	hdr->PossDupFlag = CHAR_FROM("YN");
	gen_timestamp(&hdr->SendingTime);
}

static
void gen_new_order_single(fix_message_data* data)
{
	data->hdr.MsgType = 'D';
	gen_header(&data->hdr);
	gen_random_string(data->order.Account, sizeof(data->order.Account));
	gen_random_string(data->order.ClOrdID, sizeof(data->order.ClOrdID));
	gen_timestamp(&data->order.TransactTime);
	data->order.HandlInst = CHAR_FROM("123");
	data->order.OrdType = CHAR_FROM("123456789DEGIJKLMP");
	data->order.Side = CHAR_FROM("123456789ABCDEFG");
	data->order.TimeInForce = CHAR_FROM("1234567");
	data->order.Price = gen_price();
}

// tag builders
static
void add_string_tag(FILE* const builder, unsigned tag, const char* const value)
{
	ASSURE(fprintf(builder, "%u=%s\x01", tag, value) >= 0);
}

static
void add_long_tag(FILE* const builder, unsigned tag, const long value)
{
	ASSURE(fprintf(builder, "%u=%ld\x01", tag, value) >= 0);
}

static
void add_price_tag(FILE* const builder, unsigned tag, const double value)
{
	ASSURE(fprintf(builder, "%u=%.2f\x01", tag, value) >= 0);
}

static
void add_char_tag(FILE* const builder, unsigned tag, const char value)
{
	ASSURE(fprintf(builder, "%u=%c\x01", tag, value) >= 0);
}

static
void add_timestamp_tag(FILE* const builder, unsigned tag, const utc_timestamp* const ts)
{
	ASSURE(fprintf(builder, "%u=%.04u%.02u%.02u-%.02u:%.02u:%.02u.%.03u\x01",
							tag,
							(unsigned)ts->year,
							(unsigned)ts->month,
							(unsigned)ts->day,
							(unsigned)ts->hour,
							(unsigned)ts->minute,
							(unsigned)ts->second,
							(unsigned)ts->millisecond) >= 0);
}

// message builders
static
void new_order_single_to_string(FILE* const builder, const new_order_single* const order)
{
	add_string_tag(builder, Account, order->Account);
	add_string_tag(builder, ClOrdID, order->ClOrdID);
	add_timestamp_tag(builder, TransactTime, &order->TransactTime);
	add_char_tag(builder, HandlInst, order->HandlInst);
	add_char_tag(builder, OrdType, order->OrdType);
	add_char_tag(builder, Side, order->Side);
	add_char_tag(builder, TimeInForce, order->TimeInForce);
	add_price_tag(builder, Price, order->Price);
}

static
void header_to_string(FILE* const builder, const header* const hdr)
{
	add_char_tag(builder, 35, hdr->MsgType);
	add_string_tag(builder, SenderCompID, hdr->SenderCompID);
	add_string_tag(builder, TargetCompID, hdr->TargetCompID);
	add_long_tag(builder, MsgSeqNum, hdr->MsgSeqNum);
	add_timestamp_tag(builder, SendingTime, &hdr->SendingTime);
	add_char_tag(builder, PossDupFlag, hdr->PossDupFlag);
}

static
char* make_message_body(const fix_message_data* const data, unsigned* psize)
{
	char* body;
	size_t size;
	FILE* const builder = open_memstream(&body, &size);

	ASSURE(builder);
	header_to_string(builder, &data->hdr);

	switch(data->hdr.MsgType)
	{
		case 'D':
			new_order_single_to_string(builder, &data->order);
			break;
		default:
			error(EXIT_FAILURE, 0, "Unknown message type '%c'", data->hdr.MsgType);
	}

	ASSURE(fclose(builder) == 0);
	*psize = size;
	return body;
}

// FIX message composer function
static
void message_to_string(FILE* const builder, const fix_message_data* const data)
{
	// message body
	unsigned body_size;
	char* const body = make_message_body(data, &body_size);

	// message header
	char hdr[100];
	int hdr_size = snprintf(hdr, sizeof(hdr), "8=FIX.4.4\x01" "9=%u\x01", body_size);

	ASSURE(hdr_size >= 0);

	// checksum
	unsigned char cs = 0;

	for(const char* s = hdr; s < hdr + hdr_size; ++s)
		cs += *s;

	for(const char* s = body; s < body + body_size; ++s)
		cs += *s;

	// write result
	ASSURE(fwrite(hdr, 1, hdr_size, builder) == (unsigned)hdr_size
		&& fwrite(body, 1, body_size, builder) == body_size
		&& fprintf(builder, "10=%.03u\x01", (unsigned)cs) >= 0);

	// clean-up
	free(body);
}

// message array constructor
static
const fix_message_data* make_n_messages(const unsigned n, char** str, unsigned* psize)
{
	fix_message_data* const orders = check_ptr(malloc(n * sizeof(fix_message_data)));
	char* s;
	size_t size;
	FILE* const builder = open_memstream(&s, &size);

	ASSURE(builder);

	for(unsigned i = 0; i < n; ++i)
	{
		gen_new_order_single(orders + i);
		message_to_string(builder, orders + i);
	}

	ASSURE(fclose(builder) == 0);
	*str = s;
	*psize = size;
	return orders;
}

// tests
#define NUM_MESSAGES 1000

static
bool simple_random_messages_test()
{
	bool ret = false;
	char* str;
	unsigned len;

	// construct messages
	const fix_message_data* const messages = make_n_messages(NUM_MESSAGES, &str, &len);

	// construct parser
	fix_parser* const parser = create_FIX44_parser();

	if(!parser)
	{
		REPORT_FAILURE("NULL parser");
		goto EXIT;
	}

	// parser loop
	unsigned i = 0;

	for(const fix_parser_result* res = get_first_fix_message(parser, str, len);
		res;
		res = get_next_fix_message(parser))
	{
		// check for errors
		if(!parser_result_ok(res, __FILE__, __LINE__))
		{
			print_raw_message(parser);
			goto EXIT;
		}

		// check message index
		if(i == NUM_MESSAGES)
		{
			REPORT_FAILURE("Parser unexpectedly produced too many messages");
			goto EXIT;
		}

		// validate message
		if(!valid_fix_message(res->root, messages + i))
		{
			print_raw_message(parser);
			goto EXIT;
		}

		++i;
	}

	// check for fatal errors
	const fix_error_details* const error = get_fix_parser_error_details(parser);

	if(error->code > FE_OTHER)
	{
		report_error_details(error, __FILE__, __LINE__);
		goto EXIT;
	}

	// final check of message index
	if(i != NUM_MESSAGES)
	{
		REPORT_FAILURE("Parser produced %u messages instead of %u", i, (unsigned)NUM_MESSAGES);
		goto EXIT;
	}

	// all clear
	ret = true;

EXIT:
	// clean-up
	free(str);
	free((void*)messages);
	free_fix_parser(parser);
	TEST_END(ret);
}

// entry point
void random_messages_test()
{
	srand(time(0));
	simple_random_messages_test();
}
