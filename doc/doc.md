# FullFIX

FullFIX is a library for parsing Financial Information eXchange (FIX) messages.

### Project structure

* `include/`
  * `fix.h` - main header file, contains all the data definitions and function declarations;
  * `spec.h` - some macro definitions to support the compiled specification, should not be used anywhere else.
* `src/`
  * `scanner.c` - parser, first pass;
  * `parser.c` - parser, second pass;
  * `fix_impl.h` - internal declarations;
  * `converters.c` - data conversion routines;
  * `utils.c` - helper functions.
* `test/`: unit tests
* `tools/`
  * `compile-spec` - FIX specification compiler;
  * `perf-stat` - performance statistics collector.
* `doc/`
  * `doc.md` - main documentation (this file);
  * `faq.md` - frequently asked questions.
* `Makefile` - makefile for compiling unit tests.
* `README.md` - readme file
* `LICENSE` - license file

### Architecture
The core of the library is a table-driven recursive descent parser with the control
tables generated at compile-time from a given specification in `.xml` format via
the provided compiler (file `tools/compile-spec`). The input specification is
a subset of the format used by `fix8` and `QuickFix` projects.

The nature of the FIX protocol specification dictates the use of 3 passes over
the parser input:

1. **Message extractor** (*source:* `src/scanner.c`):
During this pass the input is scanned to determine the boundaries of the next one
complete FIX message, and both message header and trailer get validated. Also, this
pass handles situations where the provided input contains an incomplete message.
2. **Parser** (*source:* `src/parser.c`): Once a complete FIX message
is extracted from the input, the message is processed by the parser step where it
gets validated against the precompiled specification and converted to an efficient
`tag -> value` mapping for further processing.
3. **Data conversion / validation**: This step is not implemented in this
library as the target "business" structures to convert the messages to are not known.
Instead, the library provides a number of conversion functions for retrieving the
value of a tag in different formats, including a string, an integer, a double, etc.
The same functions can also act as format validators.

The parser takes raw bytes as input and provides an API for extracting messages one-by-one.
Each message gets extracted and parsed (steps 1 and 2 above) and returned to the user
as an efficient `tag->value` mapping. All processing happens in the same thread of execution
to avoid any synchronisation overhead, especially  when integrating the library into 
an existing software.

### Data representation
All the definitions of the data types described below are located in the
file `include/fix.h`.

#### Strings
Strings are represented by the `fix_string` data type. The structure contains
two pointers, `begin` and `end`. The former points to the first byte of the string, and
the latter points to the first byte _after_ the end of the string. The string is _not_
guaranteed to be null-terminated and the structure does _not_ "own" the bytes it
points to. Helper functions for dealing with the strings are also provided.

**Note**: *In this library most of the strings returned by the API functions are
pointing to some internally managed memory areas which are usually overwritten
on each iteration of the parser loop. To extend the lifetime of such strings they
must be copied out, for example, using `copy_fix_tag_as_string()` function.*

#### Messages and repeating groups
FIX protocol specification describes FIX message as a sequence of `tag=value` pairs
separated by the non-printing, ASCII "SOH" (#001, hex: 0x01) delimiter character.
All tags within a message must be unique unless they are a part of a repeating group.
Repeating group is a sequence of one or more group nodes (or items), where each
node is, in turn, a sequence of `tag=value` pairs and the tags are unique within the node.

Repeating groups are represented by `fix_group` data type. The type is opaque
for the user and should only be dealt with via the provided API functions. Internally,
each group maintains an iterator which at any given point in time gives access
to one node of the group. Given a group, `fix_group* my_group`, one can iterate
over all the nodes in the group using a loop like the following:

```c
do {
  // process the node
} while(has_more_fix_nodes(my_group));
```
The internal iterator can be reset by calling `reset_fix_group_iterator()` function.
The number of nodes in a group can be retrieved using `get_fix_group_size()`
function.

Because of the similarity between FIX messages and repeating group nodes and for the
sake of uniformity, the body of each FIX message is treated as a group of only
one node.

#### Tags
Given a group with its node iterator "pointing" to a valid group node, and a tag, the
value of the tag can be retrieved by using one of the `get_tag_as_*()` functions.
Internally, the value of a tag can either be a string of type `fix_string` or a group
of type `fix_group`. The tag's value can also be retrieved converted to
another type, like, for example, a `double` or `long`. For convenience, the
specification compiler generates a header file where, amongst other things, there is
an `enum` with symbolic names of all tags from the input specification.

#### Error codes and details
Each error that may potentially happen within the parser is assigned a code.
The parser uses the error codes listed in the FIX specification as values for
`SessionRejectReason` tag from `SessionReject(3)` message (see `FIX_Transport_1.1.pdf`,
 p. 26, from http://fixprotocol.org), plus a few extra values to report some
 non-recoverable ("fatal") errors. All the codes are in `enum fix_error`.

Internally, the parser maintains one object of the type `fix_error_details`,
which gets updated upon (almost) every operation on the parser instance. The structure
includes the error code from the last operation performed, plus it gives
some extra context to produce a reasonable human-readable error message
(via `compose_fix_error_message()` function), or to fill in the relevant fields
in the `SessionReject(3)` FIX message. The structure can be accessed from a parser
handle using `get_fix_parser_error_details()` function, or via
`get_fix_group_error_details()` from a group instance.

#### Parser result
The result of each parsing operation is represented by the type `fix_parser_result`.
This type is a superset of the `fix_error_detail`. It includes
the error details of the last parsing operation, plus, upon successful parsing,
the pointer to the root node of the FIX message and the integer type code to use in the
`switch` statement for dispatching of the parsed message based on its type from the
`MsgType(35)` tag. Symbolic names for the type code are generated from
the specification.

Given an input buffer `buff` containing `n` bytes, a typical parser loop looks
like this:
```c
for(const fix_parser_result* res = get_first_fix_message(parser, buff, n);
    res != NULL;
    res = get_next_fix_message(parser))
{
    // process message
}
```

The loop terminates when either there is no complete FIX message left in the
input buffer, or a non-recoverable error has occurred (e.g., "Out of memory").
Any input bytes remaining after the loop terminates are stored internally
and will be processed upon the next invocation of the loop.

### Specification compiler
The purpose of the specification compiler is to convert the input FIX specification
(in `XML` format) to an efficient C code. Given a specification file, for example
`my-spec.xml`, the compiler generates two output files, `my-spec.h` and `my-spec.c`.
The generated header file contains two enumerations (tags and message types) and the parser
constructor function declaration. The implementation file (`my-spec.c` in this
example) contains control tables for the parser and the parser constructor. The input specification
format is the same as in the other well-known projects like `QuickFIX` and `fix8`,
though some data (like field values or `"required"` flags) are currently ignored.
It should be noted that this library is only for parsing FIX messages and so
the data related to the _outgoing_ messages should _not_ be included into the
specification.

Currently the compiler is not quite strict in validating its input, some errors in the specification can
make it through to the output without being noticed. This is one of the areas for
future improvement.

It is advised to look at the sample makefile included with the project for further
details on how to invoke the specification compiler.

### API
All the functions described below are declared in the header `include/fix.h`.

##### Parser functions
These functions operate on a parser instance.

##### _Parser constructor_
```c
fix_parser* create_fix_parser(const fix_message_info* (*parser_table)(const fix_string),
                              const fix_string fix_version)
```
Generic FIX parser constructor. Can be used to create a parser with a
hand-coded specification, but usually it is easier to use the generated parser
constructor instead.

  _Parameters:_
  * `parser_table` - parser control table entry point;
  * `fix_version` - FIX version string.

Returns newly created parser instance, or `NULL` if an error has occurred.

##### _Parser destructor_
```c
void free_fix_parser(fix_parser* const parser)
```
Releases the parser instance and frees its associated memory.

##### _FIX message iterator functions_
```c
const fix_parser_result* get_first_fix_message(fix_parser* const parser,
                                               const void* bytes, unsigned num_bytes);
const fix_parser_result* get_next_fix_message(fix_parser* const parser);
```
FIX message iterator. The first function returns non-null result if there is one
complete FIX message in the input buffer, taking into account any bytes left from
the previous run of the parser loop. The second function on each call returns one
subsequent message from the input buffer until there is no complete message left.
The input buffer must be valid for the duration of the loop.

##### _FIX parser status_
```c
const fix_error_details* get_fix_parser_error_details(const fix_parser* const parser)
```
Returns parser status.

##### _Raw FIX message_
```c
fix_string get_raw_fix_message(const fix_parser* const parser)
```
Returns the last parsed FIX message, as-is. Useful for logging.

##### FIX group functions
##### _Group iterator_
```c
bool has_more_fix_nodes(fix_group* const group)
```
Group node iterator. Moves internal iterator to the next node. Returns 'true'
if the iterator has been moved, or 'false' if the end of the group has been reached.

##### _Group iterator reset_
```c
void reset_fix_group_iterator(fix_group* const group)
```
Resets the internal group node iterator to point to the first node in the group.

##### _Group size_
```c
unsigned get_fix_group_size(const fix_group* const group)
```
Returns the number of nodes in the group.

##### _Status_
```c
const fix_error_details* get_fix_group_error_details(const fix_group* const group)
```
Returns parser status.

##### Tag access functions
All tag access functions are named following the same pattern:
```c
fix_error get_tag_as_<type>(const fix_group* const group,
                            unsigned tag,
                            <type>* const result)
```
where `type` can be one of the following:

|`<type>`| 'C' type | Description |
|--------|----------|-------------|
|`string`|`fix_string`|Returns the tag as a string, unless the tag is a group.|
|`group`|`fix_group*`|Returns the tag as a FIX group, with type validation.|
|`char`|`char`|Returns the tag as a single byte, with validation.|
|`long`|`long`|Returns the tag as long integer, or fails if cannot be converted.|
|`double`|`double`|Returns the tag as `double`, or fails if cannot be converted.|
|`boolean`|`bool`|Returns the tag as type `bool` from `<stdbool.h>`, or fails if cannot be converted.|
|`utc_timestamp`|`utc_timestamp`|Returns the tag as type `utc_timestamp` from `<fix.h>`, or fails if cannot be converted.|
|`tz_timestamp`|`tz_timestamp`|Returns the tag as type `tz_timestamp` from `<fix.h>`, or fails if cannot be converted.|
|`LocalMktDate`|`utc_timestamp`|Returns the tag as local market date, or fails if cannot be converted.|
|`fix_version`|`fix_version`|Returns the tag as type `fix_version` from `<fix.h>`, or fails if cannot be converted.|

All the functions can also act as validators if the `result` pointer is set to `NULL`,
in which case the conversion still proceeds to the end, but the final result is
not stored.

There is also a function to retrieve a malloc'ed copy of the tag value as a string:

```c
fix_error copy_fix_tag_as_string(const fix_group* const group, unsigned tag, char** const result)
```

In these functions the return code of `FE_OK` indicates that the tag is present and the conversion, if any,
has been successful, otherwise the return code indicates the kind of error encountered.
Also, the parser status gets updated with further details of the error.

##### Helper functions
##### _Time value converter_
```c
fix_error utc_timestamp_to_timeval(const utc_timestamp* const utc, struct timeval* const result)
```
Converts `utc_timestamp` to Linux `struct timeval`.

##### _Error message composers_
```c
const char* compose_fix_error_message(const fix_error_details* const details)
```
Composes a human-readable error message from the `fix_error_details`. The resulting
string is malloc'ed and thus must be freed after use.

```c
const char* fix_error_to_string(fix_error code)
```
Maps error code to a human-readable description. The resulting strings are
constant and must neither be modified nor freed.

##### _Type string to type code converter_
```c
int fix_message_type_to_code(const fix_parser* const parser, const fix_string s)
```
Returns FIX message type code corresponding to the supplied type string. Useful
for processing `RefMsgType` tag.

##### _String functions_
```c
bool fix_string_is_empty(const fix_string s)
```
Returns 'true' if the string is empty.

```c
size_t fix_string_length(const fix_string s)
```
Returns the length of the string.

```c
fix_string fix_string_from_c_string(const char* s)
```
Returns `fix_string` corresponding to the supplied C string.

```c
bool fix_strings_equal(const fix_string s1, const fix_string s2)
```
Compares two strings byte-by-byte.

```c
CONST_LIT(s)
```
Zero-overhead conversion macro, maps a string literal to `fix_string`.

### Code example
The library does not impose any particular style of programming, so all the code
below should be treated as an example only.

First of all, we define a structure for storing the parser context. The context
should at least contain the parser instance handle. In practice, such a context
usually includes some other data used for message processing.
```c
typedef struct
{
  fix_parser* parser;
  // other data
} parser_context;
```
The `parser` field is usually intialised using the generated parser
constructor function, with the appropriate error checking, and at the end of its
lifetime must be freed using function `free_fix_parser()`.

Now, assuming that the buffer `buff` has been filled in with `n` raw bytes from,
for example, a socket via `recv()` function, the parser function
may look like this:
```c
int parse(parser_context* context, const void* buff, unsigned n)
{
  // parser loop
  for(const fix_parser_result* res = get_first_fix_message(context->parser, buff, n);
      res != NULL;
      res = get_next_fix_message(context->parser))
  {
      if(result->error.code == FE_OK)
        dispatch_message(context, result->msg_type_code, result->root);
      else
        report_error(context, &result->error);
  }

  // check for fatal errors
  const fix_error_details* details = get_fix_parser_error_details(context->parser);

  if(details->code <= FE_OTHER)
    return 0;	// not a "fatal" error

  report_fatal_error(context, details);
  free_fix_parser(context->parser);	// the parser is not usable any more
  context->parser = NULL;
  return 1;
}
```

The function iterates over all the messages in the input buffer and then checks for
any fatal error in the parser. Each successfully parsed FIX message is forwarded to
a function that dispatches the message based on its type:

```c
void dispatch_message(parser_context* context, int msg_type_code, fix_group* root)
{
	switch(msg_type_code)
	{
		case NewOrderSingle:	// this constant is generated from the spec.
			process_new_order(context, root);
			break;
		// etc.
	}
}
```

Each of the error handling functions is declared like
```c
void report_error(parser_context* context, const fix_error_details* details)
```

Finally, suppose in each message processing function all we want is just to fill in
the corresponding "business" structure. For example, for the FIX message type `NewOrderSingle`
the structure may be defined like this:
```c
typedef struct
{
	char *SenderCompID, *Account, *ClOrdID;
	utc_timestamp SendingTime, TransactTime;
	char HandlInst, OrdType, Side, TimeInForce;
	double Price;
} new_order_single;
```

The following function maps a FIX message of the type `NewOrderSingle` to `new_order_single`
structure (some error handling and validation is omitted):

```c
void process_new_order_single(parser_context* context, fix_group* root)
{
	new_order_single* const order = calloc(1, sizeof(new_order_single));

	if(copy_tag_as_string(root, SenderCompID, &order->SenderCompID) != FE_OK
	|| copy_tag_as_string(root, Account, &order->Account) != FE_OK
	|| copy_tag_as_string(root, ClOrdID, &order->ClOrdID) != FE_OK
	|| get_tag_as_utc_timestamp(root, SendingTime, &order->SendingTime) != FE_OK
	|| get_tag_as_utc_timestamp(root, TransactTime, &order->TransactTime) != FE_OK
	|| get_tag_as_char(root, HandlInst, &order->HandlInst) != FE_OK
	|| get_tag_as_char(root, OrdType, &order->OrdType) != FE_OK
	|| get_tag_as_char(root, Side, &order->Side) != FE_OK
	|| get_tag_as_char(root, TimeInForce, &order->TimeInForce) != FE_OK
	|| get_tag_as_double(root, Price, &order->Price) != FE_OK)
	{
		free_new_order_single(order);	// clean-up
		report_error(context, get_fix_group_error_details(root));	// error processing
		return;
	}

	// further processing of the order
	process_order(context, order);
}
```

where the clean-up function is trivial and provided here just for the sake of completeness:

```c
void free_new_order_single(new_order_single* order)
{
	if(!order)
		return;

	if(order->SenderCompID)
		free(order->SenderCompID);

	if(order->Account)
		free(order->Account);

	if(order->ClOrdID)
		free(order->ClOrdID);

	free(order);
}
```
