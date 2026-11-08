# Approved string escape decisions

On 2026-11-07 the owner explicitly selected U1, X1, and N1. These choices are now implemented and specified in LANGUAGE_SPEC.md. The alternatives below record the reviewed tradeoffs, not additional supported modes. Stages 6 through 9 have not been started out of order.

The requested common escapes are newline (\n), carriage return (\r), tab (\t), backslash (\\), double quote (\"), NUL (\0), hexadecimal (\xHH), and braced Unicode (\u{XXXX}). Unknown, truncated, or malformed escapes should report the source filename, line, and column at the backslash. Literal source newlines and raw NUL inside a quoted string would remain errors.

## U: Unicode range and notation

- U1, recommended: one to six hexadecimal digits inside braces, representing a Unicode scalar value from U+0000 through U+10FFFF, excluding U+D800 through U+DFFF. Encode the value as UTF-8. This supports characters outside the Basic Multilingual Plane, including emoji. Empty braces, more than six digits, surrogate values, and out-of-range values are compile errors.
- U2: exactly four hexadecimal digits and only Basic Multilingual Plane scalar values, excluding surrogates. Simpler notation, but characters above U+FFFF cannot be written using this escape.

Both alternatives accept uppercase and lowercase hexadecimal digits. They reject unpaired surrogate escapes instead of creating invalid UTF-8.

## X: Meaning of exactly two hexadecimal digits

- X1, recommended: \xHH contributes one raw byte. Validate the entire decoded string as UTF-8 after all escapes are decoded. ASCII escapes work directly; a multi-byte character can be expressed by multiple byte escapes. A lone \xFF or incomplete UTF-8 sequence is a compile error. Diagnostic tracking must retain the originating escape location for invalid decoded bytes.
- X2: \xHH represents the Unicode scalar U+00HH and is then encoded as UTF-8. For example, \xE9 produces U+00E9. This keeps each escape valid text, but it is not a one-byte value for values above 0x7F.

Both alternatives require exactly two digits; any following hexadecimal character is ordinary string content. Neither silently replaces invalid input with a replacement character.

## N: Embedded NUL and I/O

- N1, recommended: preserve NUL in length-bearing strings, comparisons, redirected print output, and text file contents. File path arguments containing NUL are errors rather than silently truncated paths. The existing string ABI already carries an explicit byte length, and the runtime already rejects NUL in file paths. Console display of NUL need not show a visible glyph.
- N2: preserve NUL in string values and comparisons, but reject NUL-containing data when printing or writing text files. File paths containing NUL are also errors. This restricts text output and adds runtime checks, while keeping \0 representable as requested.

The selected policy applies consistently to NUL produced by \0, \x00, or a zero-valued Unicode escape. Import paths must reject NUL during compilation. General readText/writeText behavior must remain consistent with the chosen text-content policy; N2 would also need an explicit readText rejection rule to avoid asymmetric text I/O.

## Verification scope

The native suite tests every escape at -O0, -O2, and -O3, decoded string equality, redirected output bytes, UTF-8 file contents, embedded NUL preservation, and NUL path rejection. Negative tests cover unknown escapes, missing digits/braces, surrogate/out-of-range Unicode, invalid decoded UTF-8, raw source NUL, and exact filename/line/column reporting after a non-ASCII source character. Existing integration coverage retains unterminated-string checks. Real TextMate/Oniguruma tests cover escapes, escaped quotes, string boundaries, and malformed-escape scopes. Unicode range and decoded-byte validity are checked by the compiler rather than semantic TextMate highlighting.

After stage 5 implementation, tests, and the four required document updates, stage 6 needs a separate approval of long-literal boundaries, arithmetic promotion, overflow/wrapping, conversion behavior, and overload ranking. No integer policy is approved by choosing the string rules here.
