# JSON input: four-reference reconstruction

The reference images are Android arm64-v8a (A64), Android armabi-v7a
(A32), iOS arm64 (I64), and iOS armv7 (I32). The table contains addresses
in the corresponding image only. All entries below were located and examined
with native IDA decompilation in this investigation; the A64 parse entry is
inlined in its callers. Addresses are not executable Web constants.

| Operation | A64 | A32 | I64 | I32 |
| --- | --- | --- | --- | --- |
| Module registration | `5c2b1c` | `4f7b60` | `1001cb47c` | `1c940c` |
| evalJSON | `5c2fe0` | `4f7e68` | `1001cb7f8` | `1c9844` |
| evalJSONStorage | `5c43a8` | `4f89d4` | `1001cc750` | `1ca70c` |
| parse and finish | inlined | `4f7f68` | `1001cb8b4` | `1c994e` |
| string input constructor | `5c3124` | `4f7f0c` | `1001cb93c` | `1c99a0` |
| string input read | `5c31f0` | `4f7fe4` | `1001cb9cc` | `1c9a98` |
| string input unread | `5c3218` | `4f7ffe` | `1001cb9f4` | `1c9ab2` |
| string input finish | `5c322c` | `4f8008` | `1001cba08` | `1c9abc` |
| string input destructor | `5c31bc` | `4f7fc4` | `1001cb908` | `1c997c` |
| file input constructor | `5c4528` | `4f8aa8` | `1001cc89c` | `1ca864` |
| file input read | `5c4900` | `4f8c94` | `1001ccb3c` | `1cab06` |
| file input unread | `5c4964` | `4f8cd0` | `1001ccba0` | `1cab42` |
| file input finish | `5c4978` | `4f8cda` | `1001ccbb4` | `1cab4c` |
| file input destructor | `5c48cc` | `4f8c74` | `1001cc868` | `1ca840` |
| value | `5c3270` | `4f8034` | `1001cba40` | `1c9ae8` |
| array | `5c4108` | `4f87c8` | `1001cc494` | `1ca4a0` |
| dictionary | `5c3f04` | `4f868c` | `1001cc2c8` | `1ca2cc` |
| quoted string | `5c39b0` | `4f8490` | `1001cbfdc` | `1c9ff8` |
| whitespace and comments | `5c3864` | `4f83e0` | `1001cbe90` | `1c9f50` |
| read fixed escape characters | `5c42bc` | `4f8950` | `1001cc6b8` | `1ca6aa` |
| hex conversion | inlined | `4f88ec` | `1001cc628` | `1ca63c` |

## Shared input control flow

```text
evalJSON(text): require one argument, construct string reader from text,
  parse one value if result exists, finish reader, throw on parse error.
evalJSONStorage(path[, mode]): require one argument, construct file reader,
  parse one value if result exists, finish reader, throw on parse error.
file reader: open a read stream, read its size into vector<char>(size + 2),
  zero-terminate; FF FE selects little-endian UTF-16, mode 8 selects UTF-8,
  otherwise convert the narrow characters using the engine's conversion.
string reader: keep text, pointer, length and position; file reader keeps
  text and position. Both return -1 at end, unread decrements if nonzero,
  and finish is empty. Each owns its TJS string for its lifetime.
next: consume whitespace and #, //, or /* */ comments. A slash which does
  not start a comment is returned and its following character unread. EOF
  encountered directly in a block comment sets the error flag.
value: parse quoted strings, arrays, dictionaries, numeric tokens, $hex,
  true/false/null/void; any other token sets the input error flag.
array: create a TJS Array, put values using PropSetByNum and member-ensure;
  commas/semicolons separate entries; an omitted entry is void.
dictionary: create a TJS Dictionary, parse key and value as TJS variants,
  require a string key, accept ':' or '=' with optional '>', and PropSet
  with member-ensure.
  Commas/semicolons separate entries.
quoted string: append characters to a TJS string, support b/f/n/r/t,
  x followed by two characters, u followed by four, and copy other escapes.
  An unescaped NUL, LF or CR sets the error flag.
```

Numeric token characters are digits, `+`, `-`, `.`, `E`, and `e`; a token
containing `.` uses the engine's real conversion, all others its integer
conversion. `$` consumes hexadecimal digits and converts through a signed
32-bit accumulator. Literal booleans are integer variants, while `null` and
`void` clear the variant. The parser does not check for trailing input after
the first top-level value. A second storage argument selects mode 8 based on
the parameter pointer being present, not the integer value it contains.

The architecture and branch structure above agree across the four images;
variations include ABI layout, inlining, and the A64 compiler's inline hex
conversion. The file-input constructors all read the stream in the
decompiled body without a corresponding stream destruction there. Quoted
string readers return -1 at EOF, whereas the quoted-string loop only marks
NUL/LF/CR as errors; this is recorded as observed rather than assuming a
different EOF policy. The Web runtime's character conversion implementation
is the platform boundary for the engine conversion calls.

The input-string/key accessor checks the variant's existing type; it does not
coerce an integer or other value to a string. In A32/I64/I32 the accessor is
`4c7d84` / `10004cb80` / `4b4bc`; A64 inlines the check at the eval callback
and calls the variant-conversion error routine for a non-string. Storage's
separate type check additionally permits a void filename as a null string.
Thus the callbacks and dictionary property write must use the corresponding
strict TJS accessors rather than the converting `ttstr(variant)` constructor.

## Existing local implementation before reconstruction

`cpp/plugins/json/jsonPlugin.cpp` was excluded from the build. Its evalJSON
returned the source string, and evalJSONStorage loaded and returned text; no
reader, parser, TJS Array, or TJS Dictionary existed. Its saveJSON and
toJSONString bodies likewise returned string conversions rather than the
reference serializers. The existing CMake JSON target was commented out.
Replacing the two input bodies requires the string/file reader, parse and
finish, value, array, dictionary, string, whitespace, and hex stages above;
linking only their completed methods improves the existing absent module
without registering the nonfunctional output examples.

Before implementation, the local correspondence is therefore: the two raw
callbacks map to the respective reference wrappers; a virtual input base maps
to the read/unread/finish and error state; separate string and storage inputs
map to their constructors and reader methods; the common parse routine maps
in order to the parse-and-finish, whitespace/comment, value, string/hex,
array, and dictionary functions in the table. Array and dictionary creation,
variant ownership, and property writes must use the same TJS dispatch APIs,
not substitute native containers for the resulting script objects. The
original four raw callback names map to the registration, but the two output
callbacks remain unregistered until their distinct bodies are reconstructed.

The reference registration installs four methods. The output methods are a
separate, unfinished reconstruction and are not supplied by this input
increment. Three images use a dictionary callback that recursively writes
JSON; the I32 JSON formatter's shared callback vtable points instead to the
other serializer's callback (`342a44`), which calls its own formatter and
quote routine. The same callback vtable is referenced by both formatters in
each image. The exact reason for that difference has not been established.
The reference file writer also buffers output and its observed destructor
does not explicitly flush the remaining buffer. These findings must be
resolved and documented before enabling the existing output stubs or claiming
all four registered operations are reconstructed.

## Local comparison after input reconstruction

The following line groups are in `cpp/plugins/json/jsonPlugin.cpp`:

| Local lines | Reference step |
| --- | --- |
| 13–19 | virtual input interface and error state |
| 21–50 | whitespace, three comment forms, slash unread and block EOF error |
| 52–76 | hexadecimal conversion and fixed-count escape input |
| 78–113 | quoted string, escapes, terminator and error branches |
| 115–131 | TJS Array creation, ownership, element writes and separators |
| 133–160 | TJS Dictionary creation, key/value parse and property writes |
| 162–201 | value dispatch and number, hexadecimal and literal branches |
| 203–210 | optional result, virtual finish and parse-error exception |
| 213–229 | independent string-reader state and its virtual operations |
| 231–273 | independent storage-reader state, stream and decoding branches |
| 277–292 | callback argument checks, strict accessors and reader lifecycle |
| 295–298 | two input registrations; output registrations remain outstanding |

The `unread` behavior is retained, including the fact that a numeric token's
read past EOF can be followed by `unread`. The four references' lack of
storage-stream destruction and the nonterminating quoted-string-at-EOF
behavior remain observable risks in this faithful input increment. In all four
comment parsers, EOF immediately after a block-comment `*` is followed by an
unread and the same `*` is read again; that path also does not terminate. No
malformed-input or repeated-open runtime oracle was available in the existing
acceptance materials.
