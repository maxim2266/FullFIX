### Frequently Asked Questions
**Q.**: _This is just a parser, where is the FIX builder / composer?_

**A.**: In its simplest form, composing a FIX message is mostly a `sprintf()`
exercise, and as such is probably not worth being included into the library.
More advanced FIX builder would include an API to add individual tag/value pairs and
to convert the result to a plain string. Although this would be useful, there
would be certain overhead associated with it, which would impact the performance.
Another open issue is the amount of validation that needs to be done in this
composer / builder. In general, the correct code will produce the correct FIX messages,
which means there will be no need to validate the result. The same time, such a
validation would be useful for debugging. This leads to the idea of debug-only
validation, possibly in the spirit of `assert()` macro. Overall, at the moment
the FIX message builder / composer is an open question.

**Q.**: _What are the supported compilers / platforms?_

**A.**: The library has been tested on Linux only. The compiler used was `gcc`.
It would certainly be interesting to try `clang`, and this is in the plans.
Linux will probably remain the only target platform,
at least until Microsoft provides a C11 compiler for Windows. :)

**Q.**: _How about support for FIX protocol version 5.0?_

**A.**: For some reason, in the version 5.0 they introduced two layers to the
protocol: session layer and application layer. In my opinion, the declared benefits of the
split are by far outweighed by the complications of the implementation. Supporting
the two layers will require one more pass over the input data and one more layer of the
specification, which will certainly hurt the performance. That does not mean
the support cannot be added to this library, it all depends on if there is a substantial
amount of interest in such a support.

**Q.**: _What is the difference between this parser and [FFP](https://github.com/maxim2266/FFP)?_

**A.**: Advantages of FullFIX over the FFP parser are:
- Speed (up to x2, depending on the FIX specification size);
- Error handling more conformant to the standard;
- XML specification compiler.
 
The same time, FFP has the following advantages:
- Works for both Linux and Windows;
- Simpler specification definition via the provided C macros.
