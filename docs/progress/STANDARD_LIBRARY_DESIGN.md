# Stage 11 standard library design

Status is approved and implemented under policy B.

## Library surface before Stage 11

- `print(...)` is a statement. Its block supports `format(...)` and `input(variable)`. `type(value)` returns the static type name. The numeric type names support explicit casts.
- `append(slice, value)` extends an integer slice. `len(list)` reads a list length. String lists and explicit integer lists have collection methods. Strings support concatenation, repetition, indexing, slicing, comparison, and foreach.
- `readText`, `writeText`, `RText`, `WText`, and `AText` are available for text files. The complete existing behavior is in the [language specification](../LANGUAGE_SPEC.md).
- A quoted relative `.m` import exposes public source declarations under the existing lookup and conflict rules. The current compiler ran a relative import defining its own `sqrt`, rejected `import "std:math"` with ML3028, and reported ML3010 for an undeclared `sqrt`.

## A. Add global built-ins

Expose calls such as `sqrt(81.0)`, `textLen(value)`, `unixMillis()`, and `nextInt(min, max)` directly. Reserve each new name to prevent implicit changes in name resolution. This is easy to discover and needs less front-end work. It can reject existing functions using names such as `abs`, `min`, or `sqrt`. The naming burden grows as more functions are added.

## B. Import named standard modules

Use a quoted standard module ID and qualify calls with the file's alias.

```m
import "std:math"
import "std:text" as strings

main() {
    print(math.sqrt(81.0))
    print(strings.len("Moulang"))
}
```

- The supported IDs are `std:math`, `std:text`, `std:time`, and `std:random`. Their default aliases are the names after the colon. An optional `as identifier` applies to the standard import in the declaring file. `as` is contextual here. An alias cannot duplicate another alias, a function name, or a type name declared at file scope. Local values and parameters retain ordinary lexical precedence over aliases.
- Module aliases qualify function calls. A file must import its own alias before use. Ordinary relative imports retain their current rules and a standard alias does not propagate through them. An unknown standard ID or member reports a source-located diagnostic.
- Exports use lower camel case. Use concise names under the qualifier, such as `math.abs` and `text.len`. Preserve the global built-ins, numeric casts, and print block syntax.
- The compiler knows the exports and their types. The matching runtime archive supplies native code for operations that need it. This preserves compiler-relative distribution and avoids loading a package from the working directory or from the network. Bundled `.m` source modules could be considered later if ordinary user modules gain the same namespace and packaging needs.
- This costs more parser and semantic work than global built-ins. It isolates future additions from existing names such as a project-defined `sqrt` or the global list `len`.

## Initial API

### Math

- `math.abs`, `math.min`, and `math.max` have `int`, `long`, and `double` overloads with matching return types. Use the language's lossless argument and overload rules. `math.abs` reports a range error for the minimum signed integer. Real `abs` returns positive zero for negative zero. Ties in `min` and `max` return the first argument.
- `math.sqrt(double)` and `math.pow(double, double)` return `double`. Real math calls require finite inputs and report a runtime error for domain errors or non-finite results. A negative square-root argument is a domain error. `pow(0.0, 0.0)` returns 1.0, and zero to a negative power is a domain error. Finite underflow follows floating-point behavior.

### Text

- `text.len(value)` counts Unicode code points. `text.find(value, needle)` returns the first matching code-point index or -1. An empty needle matches at index 0. Matching is exact UTF-8 with no normalization and includes embedded NUL.
- `text.substring(value, start, end)` uses the same half-open code-point range, negative bounds, and clamping as a string slice with step one.
- `text.replace(value, needle, replacement)` replaces all non-overlapping matches from left to right. `text.split(value, separator)` returns `str[]` and preserves empty fields, including leading and trailing fields. Replace and split reject an empty needle or separator. `text.join(parts, separator)` takes `str[]` and returns a string without mutating the list. Joining an empty list returns the empty string.
- `text.lowerAscii(value)` and `text.upperAscii(value)` change ASCII letters and preserve every other code point. Full Unicode casing needs a separate choice of Unicode data and mapping rules. String and list results participate in ARC and the existing allocation limits.

### Time

- `time.unixMillis()` returns a `long` with UTC milliseconds since the Unix epoch. Wall time can move backward or forward when the clock is adjusted.
- `time.monotonicNanos()` returns a `long` suitable for elapsed-time differences within the process. The origin is unspecified and clock ticks may be coarser than one nanosecond.
- `time.formatUtcDate(unixMillis)` returns `YYYY-MM-DD` in UTC for years 0001 through 9999. Out-of-range dates raise a runtime error. Negative timestamps use the UTC day containing the instant. Local time zones and locale-specific formats require a later design.

### Random

- `random.seed(value)` accepts a `long` and resets a deterministic, program-wide SplitMix64 generator. A new process starts with seed 0. The same seed and call order reproduce the sequence. The generator is for simulation and general use.
- `random.nextInt(min, max)` returns a uniform `int` in the inclusive range. Inverted bounds raise a runtime error. Equal bounds return that value without advancing the state. Compute the range at a wider width and use rejection sampling for uniform results, including the full signed 32-bit range.
- `random.nextDouble()` returns a value in `[0, 1)` using 53 random bits. Record deterministic test vectors for the seed, integer sampling, and real sampling.

## Decision and validation

Policy B was approved. A file can use `math.sqrt` alongside a project-defined `sqrt`. The alias and export rules leave room for more standard modules with no additional global names.

Release passed 24 CTest entries and Debug passed 21. Tests cover aliases, name conflicts, relative imports, math overloads and errors, Unicode text, ARC lifetimes, fixed UTC dates, monotonic ordering, and deterministic random vectors. Debug reports zero remaining managed objects in the successful case. Relocation and PE import checks include a program that uses all four modules.

VS Code unit tests and the isolated extension source and installed VSIX suites passed. The tests verify contextual alias highlighting and function completion for an imported alias.

## Changed files

- Standard module signatures and import metadata in `include/moulang/standard.hpp`, `include/moulang/ast.hpp`, and `include/moulang/types.hpp`.
- Import parsing, source graph, semantic checks, and emission in `src/parser/parser.cpp`, `src/source/program.cpp`, `src/semantic/semantic.cpp`, and `src/codegen/llvm/object.cpp`.
- Runtime functions and static archive build in `runtime/src/standard.cpp`, `runtime/src/runtime.cpp`, and `CMakeLists.txt`.
- Example and integration checks in `examples/standard-library.m`, `tests/integration/standard-library.ps1`, `tests/integration/relocation.ps1`, and `tests/integration/static-runtime.ps1`.
- VS Code completion, grammar, and tests in `vscode-extension/src/extension.js`, `vscode-extension/syntaxes/moulang.tmLanguage.json`, `tests/vscode/grammar.test.js`, and `tests/vscode/suite.js`.
- Documentation in `README.md`, `CHANGELOG.md`, `docs/README.md`, `docs/LANGUAGE_SPEC.md`, `docs/USER_GUIDE.md`, and `docs/IMPLEMENTATION_STATUS.md`.
