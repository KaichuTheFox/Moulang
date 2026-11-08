# Moulang language specification

## Source files and entry point

The implemented target is Windows x64. The [platform isolation proposal](PLATFORM_DESIGN.md) introduces no source syntax or Linux support.

- Source files are UTF-8. The compiler accepts an initial BOM and LF, CRLF, or CR line endings.
- Compiler distribution privacy and Git history cleanup are tracked in the [privacy review](PRIVACY.md). The build path mapping applies to the C++ compiler and runtime artifacts. Moulang diagnostic source locations continue to identify the input `.m` file, including paths recorded in generated programs. The path-mapping flags do not define a new `.m` string or integer rule.
- The VS Code integration invokes `moulang run` with the active saved file as a separate process argument. Extension packaging and executable discovery do not change source syntax or evaluation rules. See the [extension test guide](../tests/vscode/README.md).
- Repository URL changes are documented in the [rename checklist](REPOSITORY_RENAME.md). They preserve `.m`, the compiler commands, and all existing language semantics.
- Compiler bundles resolve link tools relative to the executable. Keep the whole bundle and its third-party notices together. The approved Windows 10/11 x64 kit uses a separately compiled LLVM-MinGW runtime and produces single executables with Windows system DLL imports.
- Release packages preserve the same `.m` entry point and language rules as source builds. Package hash checks and hosted installation checks are deployment validation, not additional syntax or a promise of support on an untested Windows version.
- An executable has exactly one parameterless main() or void main(). Normal completion returns exit code 0.
- Braces delimit blocks. Separate statements in the same block with newlines. Inside parentheses, line breaks may separate tokens. A double slash starts a comment that ends at the next line break.
- Strings are double-quoted UTF-8 text, including empty strings. Literal CR, LF, and raw NUL are forbidden inside a quoted source string. Escaped control characters are supported under the rules below. print(value) writes a numeric, bool, or string value followed by a newline.

## String escapes

- Supported escapes are `\n` (LF), `\r` (CR), `\t` (tab), `\\` (backslash), `\"` (double quote), and `\0` (NUL). Each contributes the corresponding byte. There is no octal escape or source-line continuation.
- `\xHH` consumes exactly two hexadecimal digits and contributes one byte. A following hexadecimal digit is ordinary content: `"\x41F"` is `"AF"`. Hexadecimal digits are case insensitive. The complete decoded string must be valid UTF-8, so `"\xC3\xA9"` represents U+00E9, while a lone `"\xFF"` is a compile error.
- `\u{H...}` accepts one to six hexadecimal digits. The value must be a Unicode scalar in U+0000 through U+10FFFF, excluding U+D800 through U+DFFF. The compiler encodes it as UTF-8. Leading zeroes are allowed within the six-digit limit, and noncharacters such as U+FFFF are accepted. For example, `"\u{1F600}"` represents one supplementary Unicode character.
- Strings carry their UTF-8 byte length. Embedded NUL is data in string values, equality, print output, readText contents, and writeText contents. Neither comparisons nor output truncate at NUL. File path arguments containing NUL produce a runtime error; import paths containing NUL produce a compile error. Console rendering may not display NUL visibly.
- Unknown, incomplete, or malformed escapes produce ML1004. Invalid decoded UTF-8 produces ML1005 at the first escape that starts the invalid byte sequence. Diagnostics contain the source filename and one-based line/column; columns count Unicode source characters, with each tab counting as one character. No Unicode normalization occurs, and equality compares the decoded UTF-8 bytes.
- Use `\\` for a literal backslash in existing sources. Windows file paths can also use `/`. A single backslash no longer denotes literal text automatically. The owner approved the U1/X1/N1 policy recorded in the [design decision](STRING_ESCAPE_DESIGN.md).

## Values and expressions

- A local declaration requires an initializer. Use a type, such as int count = 10, or use var to infer it at compile time. Reassignment must respect the declared type. A local is visible within its declaring block and cannot redeclare a name in that block or a parent block.
- Numeric types are char (unsigned 8-bit), short (signed 16-bit), int (signed 32-bit), long (signed 64-bit), float (32-bit), and double (64-bit). Signed integer literals fitting int have type int; other signed 64-bit literals have type long. Larger values are compile errors. Unary-minus literals include INT_MIN and LONG_MIN. Real literals are double. char(65) prints byte value 65.
- Arithmetic and comparison use the operand type when both types match or a common type that preserves both operands' values. The common type for float and int is double. Numeric locals support compound assignment and postfix ++ and -- as statements. String equality compares UTF-8 bytes. Boolean values print as true or false.
- Integer arithmetic wraps at the type's width. Integer division truncates toward zero. Dividing an integer by zero terminates with a runtime error. Floating-point arithmetic follows IEEE 754.
- Implicit numeric conversions in assignment, arguments, and return values must preserve all source values and their precision. A short converts implicitly to float. Use float(value) for an int.
- int widens implicitly to long. long-to-int conversion requires int(value) and raises a runtime error outside INT_MIN through INT_MAX (approved R2). Other explicit integer conversions preserve low bits. Real-to-integer conversion rounds to nearest, ties to even, then preserves low bits at the destination width. NaN, infinity, and finite double-to-float overflow are runtime errors.
- A general long cannot convert implicitly to float or double. Mixed long/real arithmetic, comparisons, and conditional branches require an explicit conversion (approved P1). Mixed integer arithmetic uses the wider lossless integer type. Same-type small integer arithmetic retains its width. Thus 2147483648 is a long literal, while 2147483647 + 1 wraps as int. LONG_MIN / -1 wraps to LONG_MIN; long shifts validate the full count before narrowing it.
- Integer bitwise operators are ~, &, ^, |, <<, and >>. A shift uses the left operand's width and rejects negative counts and counts at least that width. Signed right shifts preserve the sign bit.
- Precedence runs from parentheses and postfix expressions through unary operators, multiplication and division, addition and subtraction, shifts, ordering, equality, bitwise &, ^, and |, logical && and ||, null coalescing, and the ternary conditional. Binary operators associate left, except for ??, which associates right. Logical operators and ?? skip the right operand when the left determines the result. A ternary condition must be bool, its result types must be compatible, and only the selected expression is evaluated. Ternaries associate right.

## Functions and control flow

The [diagnostic presentation proposals](DIAGNOSTIC_PROPOSALS.md) do not alter language rules or the current machine-readable diagnostic format.

- Ordinary functions in one declaring file may overload parameter types or arity (approved O1). Literal types are inferred before selection. Exact matches win; other viable conversions rank as lossless integer widening, real widening, then lossless integer-to-real conversion. Smaller sufficient destination widths win within a category. A candidate must be no worse for every argument and strictly better for at least one against every alternative; otherwise the call is ambiguous. Other supported assignment conversions rank after numeric conversions and tie when neither candidate is better. Explicit casts can resolve ambiguity. Duplicate parameter signatures, return-type-only overloads, and overloads of main are errors. Methods and constructors do not overload. Cross-module public-name conflicts and local-name precedence remain unchanged; only exported overloads are callable from another file.
- Function parameters have explicit types. Declare an explicit return type, as in int add(int a, int b), or use def to infer it. A value-returning function must return along every reachable path. Loop termination is analyzed conservatively. Recursive return type inference with circular dependencies requires an explicit return type.
- if, elif, else, and while provide branching and loops. Conditions must be bool. for (i=0 in i++(5)) evaluates its bounds once and iterates from 0 through 4. The loop variable is local to the loop. break and continue target the nearest loop.

## Slices, structs, and nullability

- int numbers[] = [10, 20, 30] and var numbers = [] create integer slices. append(numbers, 40) returns a slice with one more element and preserves the original slice length and elements. Indices start at zero. Out-of-bounds access terminates with exit code 1. Heap-backed slice buffers remain allocated until process exit.
- foreach (item in numbers) evaluates its integer slice, view, or fixed array once. Each element is copied into a local loop variable in order. break and continue are supported.
- numbers[begin:end] is a read-only, zero-copy view over the range from begin to just before end. Bounds must satisfy 0 <= begin <= end <= length. A view supports indexing and further slicing.
- Structs are named value types. Declare fields on separate lines. Position(10, 20) initializes fields in declaration order. pos.x reads a field. Structs may contain other structs and pass through function arguments and return values. Assignment copies the value. Recursive inclusion by value is rejected.
- A float field in positional initialization can accept an exactly representable integer literal directly. Convert an int variable explicitly with float(value). Field assignment and nested field assignment are supported. Copies preserve their own field values.
- Types are non-nullable by default. T? allows null for supported scalar types, structs, and classes. nickname ?? "Unknown" evaluates the fallback only for null. Direct assignment or comparisons with null can establish that a value is present in a local scope. Reassignment and control flow can change this result.
- pos?.x returns null for a null receiver and reads the field otherwise. Nullable class receivers also support safe method calls. A null check that establishes a struct value is present allows direct field access.

## Imports and file I/O

- import "modules/file.m" resolves a relative path from the importing file. Each file is processed once. A file sees its own declarations and the public declarations of files imported directly or transitively.
- Top-level functions, structs, and classes are public by default. public makes this explicit. private limits visibility to the declaring file. Local file declarations take precedence over imported public declarations. Conflicting public names, public declarations exposing private types, import cycles, and an imported main are compile errors.
- readText(path) reads a UTF-8 file into a string. writeText(path, text) writes UTF-8 and replaces an existing file. Relative paths use the program's working directory. I/O errors and invalid UTF-8 cause runtime errors.

## Experimental fixed arrays and classes

- A local fixed integer array can be declared as int values[3] = [10, 20, 30]. The length is part of its type and must match the initializer. Arrays support indexed reads and writes, value copies, and foreach.
- A view of a fixed array borrows its storage. The array cannot be written while that view is in local scope. The borrowed binding cannot be reassigned or returned from the function.
- A class is a reference type. Assignment copies the reference. Members are private by default. Methods can use this.field or the field name. new Counter(0) creates an instance. Class objects remain allocated until process exit.
- A class whose fields all have defaults can use new ClassName(). Declare one explicit constructor with public ClassName(typedParameters) { ... }. Field defaults run before the constructor body. Fields without defaults must be initialized directly in the first statements of the constructor.
- class Child : Parent provides single inheritance. The base default constructor or public zero-argument constructor runs first. A child reference can be assigned to the base type. Access to private base members, member hiding, method overriding, and multiple inheritance are compile errors.

The final fixed array notation and general lifetime and aliasing rules are open. Exception handling is planned for invalid conversions. Multiple inheritance is a future design in which method conflicts must be resolved explicitly. Base selection syntax and layout remain open.

The [memory model roadmap](ROADMAP.md#stage-9-memory-model-proposal) compares ownership, reference counting and tracing GC. Its ARC recommendation is not an approved language rule and does not change current process-lifetime heap allocations.
