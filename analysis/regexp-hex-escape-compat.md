# RegExp `\xHH` compatibility (deliberate deviation)

## Four-reference status: TJS RegExp compile

| Binary | Function | onig_new arguments |
| --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | `sub_A05600` | `OnigEncodingUTF16_LE`, `OnigSyntaxPerl`, options `flags & 0xFFF`; empty pattern → `(?:)` (UTF-16 at 0x1525c56) |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | `sub_75B930` | same (pattern end `&v10[2 * len]`) |
| Kirikiroid2_1.3.9_iOS_arm64 | `sub_100073CA0` | same (`sub_100F73C38` = onig_new) |
| Kirikiroid2_1.3.9_iOS_armv7 | `sub_70D6C` | same (`sub_D88A1C` = onig_new) |

All four throw the onig error text (the string "too short multibyte code string" is present in each:
a64 0x159bf68, a32 0xe36024, i64 0x101598725, i32 0x1427533). The local compile paths match this.

## Deviation

A game written for the Windows engine (Boost wide regex) uses
`@([a-zA-Z0-9_\x0100-\xFFFF]*)(\.[a-zA-Z0-9_]+)?`. Boost reads `\xFF` as U+00FF.
Oniguruma's Perl syntax reads `\xHH` as a crude byte. In UTF-16LE that byte is an incomplete character,
so it returns `ONIGERR_TOO_SHORT_MULTI_BYTE_STRING` (-206). The references fail identically.

With the user's approval, `TJSNewRegex` keeps the reference call. Only when that call returns -206 does it
rewrite each unescaped `\xHH` to `\x{HH}` (code point) and compile once more. If the retry also fails,
the original error is reported. Patterns accepted today compile exactly as before.

Verification: the real game fixture (Liminal Border Part I, language selection → restart). The native catch2
tests are not buildable on this Linux host (macOS-only native preset), so no unit test was added.
