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

#include "fix_impl.h"
#include <math.h>
#include <errno.h>

#define RETURN(r)	return group->error->code = (r)

// copy tag as string
fix_error copy_fix_tag_as_string(const fix_group* const group, unsigned tag, char** const result)
{
	fix_string value;
	const fix_error err = get_fix_tag_as_string(group, tag, &value);

	if(err != FE_OK)
		return err;

	if(result)
	{
		const size_t n = fix_string_length(value);
		char* const p = *result = malloc(n + 1);

		if(!p)
			RETURN( FE_OUT_OF_MEMORY );

		memcpy(p, value.begin, n);
		*(p + n) = 0;
	}

	return FE_OK;
}

// ascii digits to long converters
static
const char* convert_significant_digits(const char* s, long* const result)
{
	long res = 0;
	unsigned c = CHAR_TO_INT(*s) - '0';

	if(c == 0)
		return NULL;

	if(c <= 9)
	{
		res = c;

		for(c = CHAR_TO_INT(*++s) - '0'; c <= 9; c = CHAR_TO_INT(*++s) - '0')
		{
			const long t = res * 10 + c;

			if(t < res)	// overflow
				return NULL;

			res = t;
		}
	}

	// done
	*result = res;
	return s;
}

static
const char* convert_digits(const char* s, long* const result)
{
	// skip leading zeroes
	while(*s == '0')
		++s;

	// convert digits
	return convert_significant_digits(s, result);
}

// tag as long integer
fix_error get_fix_tag_as_long(const fix_group* const group, unsigned tag, long* const result)
{
	/* From the spec:
	 *		Sequence of digits without commas or decimals and optional sign character (ASCII characters "-" and "0" - "9" ).
	 *		The sign character utilizes one byte (i.e. positive int is "99999" while negative int is "-99999").
	 *		Note that int values may contain leading zeros (e.g. "00023" = "23"). */

	fix_string value;
	const fix_error err = get_fix_tag_as_string(group, tag, &value);

	if(err != FE_OK)
		return err;

	if(fix_string_length(value) > 20)	// ???
		RETURN( FE_INVALID_VALUE );

	// sign
	bool neg = false;

	if(*value.begin == '-')
	{
		++value.begin;
		neg = true;
	}

	// conversion
	long val;

	value.begin = convert_digits(value.begin, &val);

	// validation
	if(!value.begin || (neg && val == 0))	// overflow or '-0'
		RETURN( FE_INVALID_VALUE );

	if(value.begin < value.end)				// unprocessed bytes
		RETURN( FE_INCORRECT_VALUE_FORMAT );

	// all clear
	if(result)
		*result = neg ? -val : val;

	return FE_OK;
}

// tag as double
fix_error get_fix_tag_as_double(const fix_group* const group, unsigned tag, double* const result)
{
	/* From the spec:
	 *		Sequence of digits with optional decimal point and sign character (ASCII characters "-", "0" - "9" and ".");
	 *		the absence of the decimal point within the string will be interpreted as the float representation of an integer value.
	 *		All float fields must accommodate up to fifteen significant digits. The number of decimal places used should be
	 *		a factor of business/market needs and mutual agreement between counterparties. Note that float values may contain
	 *		leading zeros (e.g. "00023.23" = "23.23") and may contain or omit trailing zeros after the decimal
	 *		point (e.g. "23.0" = "23.0000" = "23" = "23."). */

	// About significant digits: https://en.wikipedia.org/wiki/Significant_figures

	fix_string value;
	const fix_error err = get_fix_tag_as_string(group, tag, &value);

	if(err != FE_OK)
		return err;

	// sign
	bool neg = false;

	if(*value.begin == '-')
	{
		++value.begin;
		neg = true;
	}

	// skip leading zeroes
	while(*value.begin == '0')
		++value.begin;

	// integer part
	long int_part;
	const char* s = convert_significant_digits(value.begin, &int_part);

	if(!s)
		RETURN( FE_INVALID_VALUE );

	unsigned nsig = s - value.begin;	// significant digits counter

	if(nsig > 15)
		RETURN( FE_INVALID_VALUE );

	long frac_part = 0;
	unsigned nfrac = 0;

	if(*s == '.' && *++s != SOH)
	{
		// fractional part
		value.begin = s;
		s = convert_digits(s, &frac_part);

		if(!s)
			RETURN( FE_INCORRECT_VALUE_FORMAT );

		nfrac = s - value.begin;

		if(nsig + nfrac > 15)	// counting trailing zeros as significant, contrary to the definition
			RETURN( FE_INCORRECT_VALUE_FORMAT );
	}

	// final checks
	if(s < value.end)	// unprocessed bytes
		RETURN( FE_INCORRECT_VALUE_FORMAT );

	if(neg && int_part == 0 && frac_part == 0)	// -0.0
		RETURN( FE_INVALID_VALUE );

	// compose result
	static const double factor[] = { 0., 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9, 1e-10, 1e-11, 1e-12, 1e-13, 1e-14, 1e-15 };

	double res = (double)int_part;

	if(frac_part != 0)
		res += (double)frac_part * factor[nfrac];

	if(neg)
		res = -res;

	// all done
	if(result)
		*result = res;

	return FE_OK;
}

// tag as single char
fix_error get_fix_tag_as_char(const fix_group* const group, unsigned tag, char* const result)
{
	fix_string value;
	const fix_error err = get_fix_tag_as_string(group, tag, &value);

	if(err != FE_OK)
		return err;

	if(fix_string_length(value) != 1)
		RETURN( FE_INCORRECT_VALUE_FORMAT );

	if(result)
		*result = *value.begin;

	return FE_OK;
}

// tag as boolean
fix_error get_fix_tag_as_boolean(const fix_group* const group, unsigned tag, bool* const result)
{
	char value;
	const fix_error err = get_fix_tag_as_char(group, tag, &value);

	if(err != FE_OK)
		return err;

	// conversion
	bool r;

	switch(value)
	{
		case 'Y':
			r = true;
			break;
		case 'N':
			r = false;
			break;
		default:
			RETURN( FE_INCORRECT_VALUE_FORMAT );
	}

	if(result)
		*result = r;

	return FE_OK;
}

// matchers (unsafe macros!)
#define READ_FIRST_DIGIT(s, r)	\
	switch(*(s)) {	\
		case '0' ... '9': (r) = *(s) - '0'; break;	\
		default: RETURN( FE_INCORRECT_VALUE_FORMAT );	\
	}	\
	++(s)

#define READ_DIGIT(s, r)	\
	switch(*(s)) {	\
		case '0' ... '9': (r) = (r) * 10 + *(s) - '0'; break;	\
		default: RETURN( FE_INCORRECT_VALUE_FORMAT );	\
	}	\
	++(s)

#define READ_2_DIGITS(s, r)	READ_FIRST_DIGIT((s), (r)); READ_DIGIT((s), (r))
#define READ_3_DIGITS(s, r)	READ_FIRST_DIGIT((s), (r)); READ_DIGIT((s), (r)); READ_DIGIT((s), (r))
#define READ_4_DIGITS(s, r)	READ_FIRST_DIGIT((s), (r)); READ_DIGIT((s), (r)); READ_DIGIT((s), (r)); READ_DIGIT((s), (r))

#define MATCH(s, c)	if(CHAR_TO_INT(*(s)++) != (c)) { RETURN( FE_INCORRECT_VALUE_FORMAT ); } else ((void)0)

// helper to read the 'YYYYMMDD' part of the time-stamp
static
fix_error read_date_part(const fix_group* const group, fix_string* const ps, utc_timestamp* const ts)
{
	// format 'YYYYMMDD', where YYYY = 0000-9999, MM = 01-12, DD = 01-31
	const char* s = ps->begin;

	// year
	READ_4_DIGITS(s, ts->year);

	// month
	READ_2_DIGITS(s, ts->month);

	if(ts->month == 0 || ts->month > 12)
		RETURN( FE_INVALID_VALUE );

	// day
	READ_2_DIGITS(s, ts->day);

	if(ts->day == 0 || ts->day > 31)
		RETURN( FE_INVALID_VALUE );

	// all done
	ps->begin = s;
	return FE_OK;
}

// helper to read 'HH:MM:SS' part of the time-stamp
static
fix_error read_time_part(const fix_group* const group, fix_string* const ps, utc_timestamp* const ts)
{
	// format 'HH:MM:SS.sss', where HH = 00-23, MM = 00-59, SS = 00-60 (60 only if UTC leap second).
	const char* s = ps->begin;

	READ_2_DIGITS(s, ts->hour);

	if(ts->hour > 23)
		RETURN( FE_INVALID_VALUE );

	// minute
	MATCH(s, ':');
	READ_2_DIGITS(s, ts->minute);

	if(ts->minute > 59)
		RETURN( FE_INVALID_VALUE );

	// second
	MATCH(s, ':');
	READ_2_DIGITS(s, ts->second);

	if(ts->second > 60)
		RETURN( FE_INVALID_VALUE );

	// all done
	ps->begin = s;
	return FE_OK;
}

// helper to read 'HH:MM:SS.sss' part of the time-stamp
static
fix_error read_time_ms_part(const fix_group* const group, fix_string* const ps, utc_timestamp* const ts)
{
	const fix_error err = read_time_part(group, ps, ts);

	if(err != FE_OK)
		return err;

	// milliseconds, if any
	const char* s = ps->begin;

	if(*s == '.')
	{
		++s;
		READ_3_DIGITS(s, ts->millisecond);
		ps->begin = s;
	}
	else
		ts->millisecond = 0;

	// all done
	return FE_OK;
}

// helper to read both date and time parts of the time-stamp string
static
fix_error read_timestamp_part(const fix_group* const group, fix_string* const ps, utc_timestamp* const ts)
{
	fix_error err = read_date_part(group, ps, ts);

	if(err != FE_OK)
		return err;

	MATCH(ps->begin, '-');
	err = read_time_ms_part(group, ps, ts);

	if(err != FE_OK)
		return err;

	return FE_OK;
}

// tag as utc_timestamp
fix_error get_fix_tag_as_utc_timestamp(const fix_group* const group, unsigned tag, utc_timestamp* const result)
{
	// from the spec:
	// 	string field representing Time/date combination represented in UTC (Universal Time Coordinated, also known as "GMT")
	//  in either YYYYMMDD-HH:MM:SS (whole seconds) or YYYYMMDD-HH:MM:SS.sss (milliseconds) format, colons, dash, and period required.
	// 	Valid values:
	// 	* YYYY = 0000-9999, MM = 01-12, DD = 01-31, HH = 00-23, MM = 00-59, SS = 00-60 (60 only if UTC leap second) (without milliseconds).
	// 	* YYYY = 0000-9999, MM = 01-12, DD = 01-31, HH = 00-23, MM = 00-59, SS = 00-60 (60 only if UTC leap second), sss=000-999 (indicating milliseconds).

	fix_string value;
	fix_error err = get_fix_tag_as_string(group, tag, &value);

	if(err != FE_OK)
		return err;

	utc_timestamp ts;

	err = read_timestamp_part(group, &value, &ts);

	if(err != FE_OK)
		return err;

	MATCH(value.begin, SOH);

	// all done
	if(result)
		*result = ts;

	return FE_OK;
}

// tag as tz_timestamp
fix_error get_fix_tag_as_tz_timestamp(const fix_group* const group, unsigned tag, tz_timestamp* const result)
{
	// from the spec:
	// string field representing a time/date combination representing local time with an offset to UTC to allow
	// identification of local time and timezone offset of that time. The representation is based on ISO 8601.
	// Format is YYYYMMDD-HH:MM:SS[Z | [ + | - hh[:mm]]]
	// where YYYY = 0000 to 9999, MM = 01-12, DD = 01-31 HH = 00-23 hours, MM = 00-59 minutes, SS = 00-59 seconds,
	//	hh = 01-12 offset hours, mm = 00-59 offset minutes
	fix_string value;
	fix_error err = get_fix_tag_as_string(group, tag, &value);

	if(err != FE_OK)
		return err;

	tz_timestamp ts;

	// date
	err = read_date_part(group, &value, &ts.utc);

	if(err != FE_OK)
		return err;

	// time
	MATCH(value.begin, '-');
	err = read_time_part(group, &value, &ts.utc);

	if(err != FE_OK)
		return err;

	// time zone offset
	int sign = 1;

	switch(*value.begin++)
	{
		case 'Z':
			MATCH(value.begin, SOH); // and fall through
		case SOH:
			ts.offset_minutes = 0;
			break;
		case '-':
			sign = -1; // and fall through
		case '+':
		{
			int hour, minute;

			// hour
			READ_2_DIGITS(value.begin, hour);

			if(hour < 1 || hour > 12)
				RETURN( FE_INVALID_VALUE );

			// minute
			MATCH(value.begin, ':');
			READ_2_DIGITS(value.begin, minute);

			if(minute > 59)
				RETURN( FE_INVALID_VALUE );

			// done
			ts.offset_minutes = sign * (hour * 60 + minute);
			MATCH(value.begin, SOH);
			break;
		}
		default:
			RETURN( FE_INCORRECT_VALUE_FORMAT );
	}

	// all done
	if(result)
		*result = ts;

	return FE_OK;
}

// tag as LocalMktDate
fix_error get_fix_tag_as_LocalMktDate(const fix_group* const group, unsigned tag, utc_timestamp* const result)
{
	// from the spec (FIX.5.0SP2_EP194):
	// string field representing a Date of Local Market (as oppose to UTC) in YYYYMMDD format.
	// This is the "normal" date field used by the FIX Protocol.
	// Valid values: YYYY = 0000-9999, MM = 01-12, DD = 01-31.
	// Example(s):
	//	BizDate="2003-09-10"

	// THE DOCUMENTATION IS INCONSISTENT: "2003-09-10" IS NOT IN "YYYYMMDD" FORMAT.
	// IT LOOKS LIKE MANY IMPLEMENTATIONS ACTUALLY USE THE FORMAT WITH DASHES, SO THIS
	// FUNCTION EXPECTS A STRING IN THE "YYYY-MM-DD" FORMAT.

	fix_string value;
	const fix_error err = get_fix_tag_as_string(group, tag, &value);

	if(err != FE_OK)
		return err;

	unsigned short year;
	unsigned char month, day;

	// year
	READ_4_DIGITS(value.begin, year);

	// month
	MATCH(value.begin, '-');
	READ_2_DIGITS(value.begin, month);

	if(month == 0 || month > 12)
		RETURN( FE_INVALID_VALUE );

	// day
	MATCH(value.begin, '-');
	READ_2_DIGITS(value.begin, day);

	if(day == 0 || day > 31)
		RETURN( FE_INVALID_VALUE );

	MATCH(value.begin, SOH);

	// all done
	if(result)
	{
		result->year = year;
		result->month = month;
		result->day = day;
	}

	return FE_OK;
}

// tag as FIX version
#define CODE(a, b, c)	(CHAR_TO_INT(a) + (CHAR_TO_INT(b) << 8) + (CHAR_TO_INT(c) << 16))

fix_error get_fix_tag_as_fix_version(const fix_group* const group, unsigned tag, fix_version* const result)
{
	fix_string value;
	const fix_error err = get_fix_tag_as_string(group, tag, &value);

	if(err != FE_OK)
		return err;

	// conversion
	switch(fix_string_length(value))
	{
		case sizeof("FIX27") - 1:
		case sizeof("FIX50SP1") - 1:
			if(CODE(value.begin[0], value.begin[1], value.begin[2]) == CODE('F', 'I', 'X'))
				break;
		default:
			RETURN( FE_INCORRECT_VALUE_FORMAT );
	}

	fix_version ver;
	const char* s = value.begin + 3;	// "FIX"

	switch(CODE(s[0], s[1], s[2]))
	{
		case CODE('2', '7', SOH): ver = FIX27; break;
		case CODE('3', '0', SOH): ver = FIX30; break;
		case CODE('4', '0', SOH): ver = FIX40; break;
		case CODE('4', '1', SOH): ver = FIX41; break;
		case CODE('4', '2', SOH): ver = FIX42; break;
		case CODE('4', '3', SOH): ver = FIX43; break;
		case CODE('4', '4', SOH): ver = FIX44; break;
		case CODE('5', '0', SOH): ver = FIX50; break;
		case CODE('5', '0', 'S'):
			switch(CODE(s[3], s[4], s[5]))
			{
				case CODE('P', '1', SOH): ver = FIX50SP1; break;
				case CODE('P', '2', SOH): ver = FIX50SP2; break;
				default: RETURN( FE_INCORRECT_VALUE_FORMAT );
			}
			break;
		default:
			RETURN( FE_INCORRECT_VALUE_FORMAT );
	}

	// store result
	if(result)
		*result = ver;

	// done
	return FE_OK;
}

#undef CODE

int fix_message_type_to_code(const fix_parser* const parser, const fix_string s)
{
	if(!parser || fix_string_is_empty(s))
	{
		errno = EINVAL;
		return -1;
	}

	errno = 0;

	const fix_message_info* const info = parser->parser_table(s);

	return info ? info->message_type : -1;
}
