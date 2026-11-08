# Diagnostic quality proposals (stage 8; no renderer changes)

These ten messages were collected by running the current Release compiler with check on separate one-line files. Paths below are the actual relative fixture names. The goal is to explain the concrete mistake and a likely next action without requiring C++ terminology. Proposed wording and hints are not implemented.

1. `main() { print(missing) }` produces `case1.m:1:16: error ML3004: Undeclared variable 'missing'.` Suggest: "No variable named 'missing' is visible here." Hint: declare it before this line or check spelling.
2. `main() { int n = true }` produces `case2.m:1:14: error ML3007: Variable and expression types must match.` Suggest: "'n' needs int, but this value is bool." Hint: use a bool variable if true/false was intended. The diagnostic should underline the incompatible expression, not only '='.
3. `main() { print("\q") }` produces `case3.m:1:17: error ML1004: Unknown string escape. Use a supported escape or escape the backslash.` Suggest: "Unknown escape '\q'." Hint: write '\\q' for a literal backslash followed by q; list supported escapes in linked documentation.
4. `main() { print(9223372036854775808) }` produces `case4.m:1:16: error ML3003: Integer literal is outside the signed 64-bit range.` Suggest: "This integer is too large for long (maximum 9223372036854775807)." Do not recommend double without explaining precision loss.
5. `main() { print(long(1) + 0.5) }` produces `case5.m:1:24: error ML3005: Mixed long and real values require an explicit conversion.` Suggest: "Cannot add long and double implicitly." Hint: use double(value) if floating-point rounding is acceptable.
6. `main() { if (1) {} }` produces `case6.m:1:10: error ML3014: A condition must have type bool.` Suggest: "if needs true or false; this condition is int." Hint: write the comparison you mean, such as value != 0. Do not automatically change source.
7. `main() { print(noSuchFunction()) }` produces `case7.m:1:16: error ML3010: Unknown callable function.` Suggest: "No visible function named 'noSuchFunction'." Hint: check its name, import, and public visibility. Distinguish this from an existing overload group with no matching arguments.
8. `main() { var x = null }` produces `case8.m:1:14: error ML3019: Specify a nullable type when initializing with null.` Suggest: "null does not tell us the type of 'x'." Hint: specify the intended nullable type, for example int? x = null.
9. `main() { int a[2] = [1] }` produces `case9.m:1:21: error ML3032: Fixed array initializer length does not match the declaration.` Suggest: "Array 'a' needs 2 elements, but the initializer has 1." Offer either adding an element or changing the declared length, without choosing for the user.
10. `main() { print(int("123")) }` produces `case10.m:1:16: error ML3020: A numeric cast requires exactly one numeric value.` Suggest: "int(value) needs a number; this value is string." Do not suggest a string parsing API until one exists.

## Proposed source display

Preserve the machine-readable filename:line:column, severity and ML code on the first line so VS Code problem matching remains compatible. Add a separate source line, an underline and an optional hint:

```text
case1.m:1:16: error ML3004: No variable named 'missing' is visible here.
  1 | main() { print(missing) }
    |                ^^^^^^^
hint: Declare it before this line or check its spelling.
```

Use source spans rather than fabricated columns. Renderer work must define tab display width, Unicode character versus terminal-cell width, supplementary characters, CRLF, long-line clipping and multi-file/unsaved source storage. Keep terminal control bytes escaped in displayed source. An optional plain/machine format or JSON diagnostic stream would avoid breaking tools that expect one line per error; this is a proposal requiring its own design decision.

## Future validation

Add snapshots for the ten examples, imported files, unsaved overlays, Unicode/tabs, long lines and runtime sites. Assert machine-readable first lines remain compatible and test VS Code navigation. No new renderer, source underline, suggestion engine or JSON protocol is claimed here. Existing diagnostic tests and document-link checks remain the current validation.
