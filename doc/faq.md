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

**Q.**: _How about support for FIX protocol version 5.0?_

**A.**: For some reason, in the version 5.0 they introduced two layers to the
protocol: session layer and application layer. In my opinion, the declared benefits of the
split are by far outweighed by the complications of the implementation. Supporting
the two layers will require one more pass over the input data and one more layer of the
specification, which will certainly hurt the performance. The FIX protocol
used to be good for quick exchange of very focused financial data
and assumed a reasonably simple implementation. Not any more. I think the authors of the
standard should at some point at least attempt to implement their protocol in
the real code to assess on the quality of some of their decisions. That does not mean
the support cannot be added to this library, it all depends on if there is a substantial
amount of interest in such a support.
