# FullFIX

###### Platform: Linux

###### Licence: BSD 3-clause

###### Status: Beta release

FullFIX is a library for parsing Financial Information eXchange (FIX) messages.
The main goal of the project is to produce the fastest software solution for FIX protocol.
The library is written entirely in C for better portability.
The parser depends on `libc` only and it does not impose any I/O or threading model.
Unlike some other well known solutions, in this parser the FIX specification
gets converted to efficient C code at compile time to achieve the best performance.

_Supported FIX protocol versions_: up to and including version 4.4.

### Performance

The numbers below have been achieved on Intel Core i5-8500T 2.10GHz processor.
On modern production hardware the results will probably be better.

_Compiler:_ gcc (Ubuntu 7.3.0-27ubuntu1~18.04) 7.3.0

_OS:_ Linux Mint 19 64bit

FIX message type                  | FIX specification                        | Validation | Average time to parse one message
----------------------------------|------------------------------------------|------------|----------------------------------
NewOrderSingle('D')               | Hand-coded spec. for this message only   | No         | 0.152 µs/msg
NewOrderSingle('D')               | Hand-coded spec. for this message only   | Yes        | 0.262 µs/msg
NewOrderSingle('D')               | Compiled full spec. for FIX.4.4          | Yes        | 0.324 µs/msg
MarketDataIncrementalRefresh('X') | Hand-coded spec. for this message only   | Yes        | 0.487 µs/msg
MarketDataIncrementalRefresh('X') | Compiled full spec. for FIX.4.4          | Yes        | 0.611 µs/msg

For more details see `doc/` directory of the project.
