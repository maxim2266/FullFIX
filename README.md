# UltraFIX

UltraFIX is a library for parsing Financial Information eXchange (FIX) messages.
The main goal of the project is to produce the fastest software solution for FIX protocol.
The parser does not rely on any library apart from `libc`, it does not require any
special memory allocator and it does not impose any I/O or threading model.
The library is written entirely in C for better portability. Unlike some other well known solutions,
in this parser the FIX specification gets converted to C code at compile time to achieve the best performance.

### Performance

The numbers below are achieved on my 5 years old laptop with Core i5-430M 2.25GHz processor,
on the real production hardware the results will probably be better.

Compiler: gcc (Ubuntu 4.8.4-2ubuntu1~14.04) 4.8.4

OS: Linux Mint 17.2 64bit

FIX message type                  | FIX specification                        | Validation | Time to parse one message (average from 100 runs)
----------------------------------|------------------------------------------|------------|--------------------------------------------------
NewOrderSingle('D')               | Hand-coded spec. for this message only   | No         | 0.335 µs/msg
NewOrderSingle('D')               | Hand-coded spec. for this message only   | Yes        | 0.571 µs/msg
NewOrderSingle('D')               | Compiled full spec. for FIX.4.4          | Yes        | 0.754 µs/msg
MarketDataIncrementalRefresh('X') | Hand-coded spec. for this message only   | Yes        | 1.294 µs/msg
MarketDataIncrementalRefresh('X') | Compiled full spec. for FIX.4.4          | Yes        | 1.435 µs/msg

###### Platform: Linux

###### Licence: BSD 3-clause
