# Stage 7 compile diagnostic design

The ARC Release compiler reproduced the diagnostic lines for all ten existing [examples](../DIAGNOSTIC_PROPOSALS.md). Each command exited with code 1 and wrote the diagnostic to standard error. The owner approved option A, and the [user guide](../USER_GUIDE.md#read-diagnostics) documents the implemented options.

## Output policies

| Option | Default behavior | Trade-off |
| --- | --- | --- |
| A. Rich output on an interactive terminal | Show the source, caret, explanation, and relevant hint when standard error is a console. Use the current single-line format when standard error is redirected. Support explicit plain and rich modes. | Helps interactive users immediately. Terminal detection and VS Code task terminals need regression coverage. |
| B. Plain output with opt-in rich mode | Keep single-line output in all environments. Show the added lines only when a user passes the rich option. | Keeps every existing invocation's output stable. Users need the option each time they want source context. |

**Recommendation is A.** Add `--diagnostic-format=plain` and `--diagnostic-format=rich` for `check`, `build`, and `run`, with the console check as the default. An explicit option takes precedence over console detection. Keep the existing first line, English text, ML code, source coordinates, severity, standard error stream, and exit code in the first rollout. Add more specific explanations and hints on separate, indented lines. Existing automated callers that use redirected standard error and VS Code live diagnostics will continue to receive the same single line. VS Code task terminals may show richer output while the anchored Problems matcher still reads the first line.

## Source and location

- Represent an optional highlight span separately from the primary diagnostic location. Keep the current one-based code-point coordinates in the machine-readable line. Use lexer token boundaries and parser or semantic expression locations for the highlight. Use a single caret at the known point when an end position is unavailable.
- Preserve the UTF-8 text actually parsed for each file in rich mode. Index it using the existing file ID and retain it through parsing and semantic analysis, including for imported files. Use the unsaved standard-input overlay for the entry file when `check --stdin` is active. A missing snapshot or an invalid source encoding produces the existing header and any applicable explanation.
- Expand tabs to a documented four-column stop. Render source, the caret, and clipping through the same column mapping. Use a deterministic display policy for combining marks and ambiguous-width characters. Escape terminal control and bidirectional formatting characters in continuation lines. Handle BOM, CRLF, missing final newlines, supplementary characters, and empty or clipped lines. A multi-line highlight displays the first affected line in this rollout.
- The renderer uses two display cells for precomposed Hangul syllables and escapes other non-ASCII code points. This keeps combining marks, supplementary characters, and control characters visible in a deterministic spelling.
- Preserve the distinction between compiler code-point columns and VS Code UTF-16 offsets. The live extension already converts the primary column to a VS Code position. The task problem matcher reads the numeric column directly. Test the existing task behavior, including paths with drive letters and spaces, and preserve its protocol.

## Initial messages

The following cases add context underneath the original header. Hints depend on the error context and may be omitted when no clear action is available. The exact current error strings and one-line test inputs are in the [baseline](../DIAGNOSTIC_PROPOSALS.md).

| Case | Highlight | Explanation and hint |
| --- | --- | --- |
| 1, ML3004 | Unknown identifier | The named variable is outside the visible scope or undeclared. Show a close visible name when there is a single clear match. |
| 2, ML3007 | Right-hand expression | The assignment expects int and received bool. Show the actual type names and explain that the declaration may need bool if a truth value was intended. |
| 3, ML1004 | Backslash and escape character | The escape is unsupported. Show the supported spelling for a literal backslash and link to the escape rules in documentation. |
| 4, ML3003 | Oversized integer token | The literal exceeds the signed long range. Show the maximum for a positive overflow. |
| 5, ML3005 | Operator and operand types | The operation combines long and double. Explain explicit conversion and the possibility of precision loss. |
| 6, ML3014 | Condition expression | The condition expects bool and received int. Suggest an explicit comparison when the intended condition is apparent. |
| 7, ML3010 | Call name | The callable name is not visible. Suggest a visible name if it is unambiguous, or point to import and visibility rules. |
| 8, ML3019 | Null initializer | The initializer cannot determine the underlying type. Show a nullable annotation example. |
| 9, ML3032 | Initializer | Show expected and actual array lengths. Suggest correcting the declared length or initializer. |
| 10, ML3020 | Cast argument | A numeric cast expects a number and received str. State the accepted argument types. |

## Name suggestions and Korean

- Compare Unicode code points against identifiers in the same accessible scope. For an unknown call, consider visible callable names. For an unknown variable, use visible variables and eligible fields. Keep existing case sensitivity and normalization rules. Use bounded edit distance and emit a suggestion only for a unique best match within a conservative threshold. Do not suggest private imports or substitute names for overload or argument errors.
- Start with English explanations. Korean is feasible through a message catalog keyed by a separate message identifier and typed arguments. One ML code can represent several contexts, so the code alone is insufficient as a translation key. A later explicit locale selection can translate rich explanations and hints while retaining the canonical English header and machine-readable fields. Test Hangul output in the Windows console and redirected UTF-8. Locale switching requires a separate decision about catalog scope and fallback behavior.

## Verification

1. Compare the current ten exact plain outputs and add rich snapshots for the same inputs. Confirm output streams and exit status for check, build, and run, including failure before user-program execution.
2. Verify imported-file errors, edited entry overlays, a changed on-disk source, missing source context, and invalid UTF-8. Test tabs, CRLF, Hangul, a supplementary character, combining and control characters, long lines, and an error at EOF.
3. Exercise unique matches, ties, private names, and overload failures. Verify both forced output modes on a console and a redirected process.
4. Check saved-file Problems and live unsaved diagnostics in the isolated VS Code source and VSIX suites. Confirm that continuation lines do not create duplicate Problems entries.
5. Run Debug and Release CTest and documentation checks. Update the user guide with the selected default and how to request single-line output.

The diagnostic regression test covers all ten legacy headers and rich excerpts, command options, imported and unsaved sources, invalid encoding, Unicode display, and name visibility. A local Windows pseudoconsole probe verified automatic, forced rich, and forced plain modes. Isolated VS Code source and VSIX runs confirmed saved-file Problems and unsaved diagnostics. Debug and Release passed CTest. The hosted workflow remains a separate verification environment.
