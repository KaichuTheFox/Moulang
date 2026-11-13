# Moulang language specification

## Source files and entry point

- Source files are UTF-8. The compiler accepts an initial BOM and LF, CRLF, or CR line endings.
- An executable has exactly one parameterless main() or void main(). Normal completion returns exit code 0.
- Braces delimit blocks. Separate statements in the same block with newlines. Inside parentheses, line breaks may separate tokens. A double slash starts a comment that ends at the next line break. `/* ... */` comments may span lines and do not nest. Line breaks inside a block comment can separate statements. An unclosed comment reports ML1006 at the opening delimiter. Quotes protect comment markers inside string literals.
- Identifiers start with an ASCII letter, underscore, or Hangul letter and may continue with those characters or ASCII digits. Hangul syllables and Jamo are supported. The compiler validates UTF-8 and compares identifiers without Unicode normalization. For example, a precomposed Hangul syllable and its equivalent sequence of Jamo are distinct identifiers.
- Strings are double-quoted UTF-8 text of type `str`, including empty strings. The old type spelling `string` reports ML2002. Literal CR, LF, and raw NUL are forbidden inside a quoted source string. Escaped control characters are supported under the rules below.

## String expressions and output

- `a + b` concatenates two strings. `s * n` repeats a string an `int` number of times. Zero repetitions produce an empty string. Negative repetition counts, byte lengths beyond the signed 32-bit limit, and exhaustion of the dynamic storage budget produce runtime errors.
- `s[i]` returns one Unicode code point as a `str`. A negative index counts from the end. An index outside the string terminates with a runtime error. ASCII strings, including embedded NUL, use O(1) indexing. For other strings, a nonnegative index scans forward from the first code point and a negative index scans backward from the last code point. A valid index `i` takes O(i + 1) time from the front or O(|i|) from the end, with O(n) worst-case time for n code points and O(1) extra space.
- `s[start:end:step]` takes a half-open slice in code points. Start and end may be omitted. Omitting the second colon and step selects a step of one. Negative bounds count from the end and outlying bounds clamp to the appropriate edge. A negative step traverses backward, with omitted start at the last code point and omitted end before the first. A zero step is a runtime error. These operations can split combining-character sequences.
- Strings are immutable. Indexing and contiguous slicing can share the original bytes, and the returned value keeps those bytes alive. Dynamic string storage is reclaimed when its last owning reference is released. Live string-expression and list allocations share a 256 MiB budget.
- `foreach (part in text)` evaluates the string once and walks its UTF-8 code points in order. Each `part` is a `str` with one code point. Combining marks are separate items. An empty string has no iterations. Reassigning the original variable or the loop variable leaves the sequence being traversed intact. Break, continue, and return release the appropriate loop-owned values.
- `print(a, b, c)` evaluates arguments left to right before writing the line. It renders numeric, bool, and string values separated by one space, then writes a newline. It requires at least one argument.
- `type(expr)` checks exactly one expression and returns its static type name as a `str`. The argument has no runtime evaluation. Type names cover built-ins, nullable types, nominal types, and existing integer collections.

## Print blocks

```m
main() {
    str name = "Sam"
    int score = 0
    print("{}'s score?") {
        format(name)
        input(score)
    }
    print(name, score)
}
```

- A print block contains at most one `format(...)`, followed by zero or more `input(variable)` items. Put the opening brace on the same line as the print call and separate block items with newlines. The built-in block items are available only in this block. Their spellings remain available to user declarations.
- Formatting takes one `str` template in the print call. Each `{}` consumes the next scalar argument. Use `{{` for a literal opening brace and `}}` for a literal closing brace. Unmatched braces and too few or too many arguments cause runtime errors before any template text is written. Without a format item, braces in text print literally.
- Evaluate the print arguments first, then the format arguments from left to right. Side effects of these expressions occur before template validation and are retained on error. The print statement writes the complete line and its final newline before the first input item executes.
- Each input item reads a line and assigns it to an existing local variable or parameter of non-nullable type `str`, `bool`, or a numeric type. Windows console input uses Unicode. Redirected input must be UTF-8. The line terminator is removed, and a nonempty last line may end at EOF without a newline. Lines may contain at most 1 MiB of UTF-8 data. Reading with no data left produces a runtime error.
- String input preserves the line's other bytes, including spaces and NUL. Numeric and bool input ignore leading and trailing ASCII spaces and tabs. Integers use decimal notation, reals allow decimal exponents, and bool accepts `true` or `false`. Invalid, out-of-range, and non-finite numbers are runtime errors. Each successful input assignment takes effect before the next item.

## String escapes

- Supported escapes are `\n` (LF), `\r` (CR), `\t` (tab), `\\` (backslash), `\"` (double quote), and `\0` (NUL). Each contributes the corresponding byte. There is no octal escape or source-line continuation.
- `\xHH` consumes exactly two hexadecimal digits and contributes one byte. A following hexadecimal digit is ordinary content: `"\x41F"` is `"AF"`. Hexadecimal digits are case insensitive. The complete decoded string must be valid UTF-8, so `"\xC3\xA9"` represents U+00E9, while a lone `"\xFF"` is a compile error.
- `\u{H...}` accepts one to six hexadecimal digits. The value must be a Unicode scalar in U+0000 through U+10FFFF, excluding U+D800 through U+DFFF. The compiler encodes it as UTF-8. Leading zeroes are allowed within the six-digit limit, and noncharacters such as U+FFFF are accepted. For example, `"\u{1F600}"` represents one supplementary Unicode character.
- Strings carry their UTF-8 byte length. Embedded NUL is data in string values, equality, print output, readText contents, and writeText contents. Neither comparisons nor output truncate at NUL. File path arguments containing NUL produce a runtime error; import paths containing NUL produce a compile error. Console rendering may not display NUL visibly.
- Unknown, incomplete, or malformed escapes produce ML1004. Invalid decoded UTF-8 produces ML1005 at the first escape that starts the invalid byte sequence. Diagnostics contain the source filename and one-based line/column; columns count Unicode source characters, with each tab counting as one character. No Unicode normalization occurs, and equality compares the decoded UTF-8 bytes.
- Use `\\` for a literal backslash in existing sources. Windows file paths can also use `/`. A single backslash no longer denotes literal text automatically.

## Values and expressions

- A local declaration requires an initializer. Use a type, such as int count = 10, or use var to infer it at compile time. Reassignment must respect the declared type. A local is visible within its declaring block and cannot redeclare a name in that block or a parent block.
- Numeric types are char (unsigned 8-bit), short (signed 16-bit), int (signed 32-bit), long (signed 64-bit), float (32-bit), and double (64-bit). Signed integer literals fitting int have type int; other signed 64-bit literals have type long. Larger values are compile errors. Unary-minus literals include INT_MIN and LONG_MIN. Real literals are double. char(65) prints byte value 65.
- Arithmetic and comparison use the operand type when both types match or a common type that preserves both operands' values. The common type for float and int is double. Numeric locals support compound assignment and postfix ++ and -- as statements. String equality compares UTF-8 bytes. Boolean values print as true or false.
- Integer arithmetic wraps at the type's width. Integer division truncates toward zero. Dividing an integer by zero terminates with a runtime error. Floating-point arithmetic follows IEEE 754.
- Implicit numeric conversions in assignment, arguments, and return values must preserve all source values and their precision. A short converts implicitly to float. Use float(value) for an int. A real literal used to initialize or assign a float is accepted when the literal's double value is exactly representable as float, for example `float b = 2.5`.
- int widens implicitly to long. Every conversion from a wider integer type to a narrower integer type requires an explicit cast. The cast raises a runtime error at its source location if the value falls outside the destination range. The ranges are 0 through 255 for char, -32768 through 32767 for short, and -2147483648 through 2147483647 for int. The check runs when the cast executes, including casts of integer literals.
- Real-to-integer conversion rounds to nearest, ties to even, then preserves low bits at the destination width. NaN and infinity cause runtime errors when converted to integers. Finite double values beyond the float range cause runtime errors when narrowed to float.
- A general long cannot convert implicitly to float or double. Mixed long/real arithmetic, comparisons, and conditional branches require an explicit conversion. Mixed integer arithmetic uses the wider lossless integer type. Same-type small integer arithmetic retains its width. Thus 2147483648 is a long literal, while 2147483647 + 1 wraps as int. LONG_MIN / -1 wraps to LONG_MIN; long shifts validate the full count before narrowing it.
- Integer bitwise operators are ~, &, ^, |, <<, and >>. A shift uses the left operand's width and rejects negative counts and counts at least that width. Signed right shifts preserve the sign bit.
- Precedence runs from parentheses and postfix expressions through unary operators, multiplication and division, addition and subtraction, shifts, ordering, equality, bitwise &, ^, and |, logical && and ||, null coalescing, and the ternary conditional. Binary operators associate left, except for ??, which associates right. Logical operators and ?? skip the right operand when the left determines the result. A ternary condition must be bool, its result types must be compatible, and only the selected expression is evaluated. Ternaries associate right.

## Functions and control flow

- Ordinary functions in one declaring file may overload parameter types or arity. Literal types are inferred before selection. Exact matches win; other viable conversions rank as lossless integer widening, real widening, then lossless integer-to-real conversion. Smaller sufficient destination widths win within a category. A candidate must be no worse for every argument and strictly better for at least one against every alternative; otherwise the call is ambiguous. Other supported assignment conversions rank after numeric conversions and tie when neither candidate is better. Explicit casts can resolve ambiguity. Duplicate parameter signatures, return-type-only overloads, and overloads of main are errors. Methods and constructors do not overload. Cross-module public-name conflicts and local-name precedence remain unchanged; only exported overloads are callable from another file.
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

## Integer lists

- Declare a shared mutable list with `int[] numbers = [10, 20]`. The prefix type can also appear in parameters, return types, and fields. A directly typed literal may be empty. `int numbers[]` and an integer literal without list context still create the integer slices described above.
- Assignment, passing an argument, and returning an `int[]` share the same list. A separate list can be filled by iterating and appending. The standalone `append(slice, value)` applies to integer slices.
- Index reads and writes accept zero-based or negative indices. `append` adds an element, `insert` inserts before a normalized index and accepts `len(numbers)`, and `pop` removes and returns the final element. `remove` deletes the first matching integer. `sort` orders signed integers from smallest to largest. Invalid indices, removal of an absent value, and popping an empty list raise runtime errors.
- `len(numbers)` returns the list length. `print(numbers)` renders bracketed integers separated by commas and spaces. List methods support post-mutation blocks.
- `foreach (value in numbers)` retains and visits the list once. Replacing an element during traversal is allowed. A later iteration reads the updated value. Changing the length or order through any alias raises a runtime error at the operation. Sorting a list that is already in order is allowed.
- `numbers[begin:end]` creates a read-only snapshot view. The bounds must satisfy `0 <= begin <= end <= len(numbers)`. A view supports zero-based indexing, further slicing, and foreach. It remains valid after the list is changed or released. A view retains the backing buffer, and a later write copies the buffer if a view still needs it. ARC reclaims the list and each buffer after its final owner releases it. Allocations use the shared live storage budget.

## String lists

- Declare `str[] songs = ["A", "B"]` or infer the same type with `var songs = ["A", "B"]`. Use an explicit list type for an empty initializer. An untyped `[]` remains an empty integer slice.
- Lists are references. Assignment, parameter passing, and returns share the list object. Mutation through any alias changes the shared list. Reassigning a variable to a different list changes that variable's binding.
- `songs[i]` reads or writes one element. Indices have type `int`, start at zero, and accept negative offsets from the end. An out-of-bounds read or write is a runtime error. Indexed assignment evaluates the receiver, index, and replacement in that order.
- `songs.append(value)` adds to the end. `songs.insert(i, value)` inserts before the normalized index and accepts `len(songs)` to append. Both require a value of the list's element type. Out-of-bounds insertions are runtime errors.
- `songs.remove(value)` removes the first byte-equal string and reports a runtime error if absent. `songs.sort()` sorts in ascending UTF-8 byte order. `songs.pop()` removes and returns the last element and reports a runtime error on an empty list. `len(songs)` returns the number of elements as an `int`.
- A standalone list mutation may be followed by a block on the same line. The mutation runs before the block, and a runtime error prevents the block from running. Variables declared in the block are local to it.
- `print(songs)` renders bracketed, quoted strings separated by comma and space. ASCII controls, quotes, and backslashes are escaped. A list argument is read when the line is rendered, so earlier references reflect mutations from subsequent argument evaluation.
- List objects retain their elements and release them on removal, replacement, or destruction. Popping an element transfers that reference to the result. The last owning reference to a list releases its slot storage and its remaining elements. List growth uses the shared 256 MiB live storage budget.
- Repeated brackets declare nested lists, as in `str[][] table = [["A"], ["B", "C"]]`. Rows may have different lengths or be empty. Indexing a nested list returns the shared child list. Each bracket pair checks bounds separately. Assigning to `table[i]` replaces a row, and assigning to `table[i][j]` changes one string.
- Nested inference requires a known element type at each level. `[[], ["A"]]` infers `str[][]`; `[[], []]` needs an explicit type. Source supports one or two dimensions. `append`, `insert`, and `pop` operate on the current level's element type. `sort` and `remove` apply to flat `str[]` lists.
- `foreach (item in list)` evaluates its source once and visits its slots in order. The local `item` is a `str` for a flat list and a shared `str[]` row for a nested list. An empty list has no iterations. Reassigning the source or loop variable changes only that variable's binding.
- Indexed assignment may replace a string or row during iteration. A replacement at a later slot is visible when the loop reaches it. The current loop variable retains the value already read from its slot.
- A structural change to an actively iterated list raises a runtime error at the operation, including changes through aliases. Adding, removing, and reordering slots are structural changes. Sorting a flat list already in order leaves its slots unchanged and is allowed. The guard applies to the specific list being traversed. A nested list's child row can be changed independently while the outer list is being traversed, provided the child has no active iterator.
- Each iterator retains its list until normal exit, `break`, or `return`. `continue` releases the current item and keeps the iterator active.
- Mixed element types are rejected. The [mixed-list proposal](MIXED_LIST_DESIGN.md) records the changes needed to support them.

## Imports and file I/O

- `.mcx` is the default source suffix. `.m`, `.mxx`, `.mpp`, `.mcp`, `.m++`, and `.mc+` use the same grammar and compiler. The CLI opens an explicitly named entry file using the path provided.
- `import "modules/file.mcx"` resolves a relative path from the importing file. Imports accept the seven exact lowercase suffixes and may mix them in one program. The `+` characters in an import are part of the filename. An explicit `.M` suffix in an import reports ML3028. After the suffix check, the filesystem resolves the path using the directory's case rules.
- Each file is processed once. A file sees its own declarations and the public declarations of files imported directly or transitively.
- Top-level functions, structs, and classes are public by default. `public` makes this explicit. `private` limits visibility to the declaring file, including when that file is imported transitively. Local declarations take precedence over imported public declarations.
- A public function name declared by two different source files conflicts even when their parameter types or arities differ. Overloads declared within one file follow the usual overload rules. Conflicting public type names, public declarations exposing private types, source import cycles, and an imported `main` are compile errors.
- Use `moulang check module.mcx --library` to check a source module without a `main`. A program builds its imported source modules together with its entry point.
- `readText(path)` and its alias `RText(path)` read a UTF-8 file into a `str`. Invalid file UTF-8 raises a runtime error. `writeText(path, text)` and its alias `WText(path, text)` write the given bytes and replace an existing file.
- `AText(path, text)` creates a file or appends the given UTF-8 bytes at the end of an existing file. It leaves the existing bytes intact, inserts no newline, and does not decode the existing file. Writes can be partial on I/O failure. Concurrent appends can interleave between chunks.
- File operations resolve relative paths from the program's working directory. Paths must be nonempty and cannot contain NUL. Embedded NUL remains valid in the file contents. I/O failures terminate with a source-located runtime error.
- File-read strings follow the same lifetime rules as other strings. Their buffers use the file size limit and separate accounting.

## Standard modules

- `import "std:math"` binds `math` in the current source file. An optional `as name` chooses a different alias, as in `import "std:text" as strings`. Use the alias in qualified calls such as `math.sqrt(81.0)`. An alias cannot conflict with another alias or a function or type declared in the same file. A local binding, parameter, or instance field takes precedence over the alias.
- A standard import has file-local scope. A source file that imports another file does not acquire that file's aliases. Relative source imports retain their existing lookup and visibility. Unknown standard IDs and conflicting aliases report ML3044. Unknown qualified standard functions report ML3045.
- `std:math` exports `abs`, `min`, and `max` for `int`, `long`, and `double`. `sqrt(double)` and `pow(double, double)` return `double`. Overloads use the usual lossless conversion rules. Integer `abs` rejects the smallest value of its type. Real functions reject non-finite arguments and domain errors or non-finite results. `sqrt` rejects negative values. `pow(0.0, 0.0)` returns 1.0. `min` and `max` return the first argument on ties.
- `std:text` exports `len` and `find` in Unicode code points. `find` returns the first index, -1 for no match, and 0 for an empty needle. `substring(value, start, end)` follows the existing half-open string slice rules. `replace` replaces non-overlapping matches from left to right. `split` returns a `str[]` and preserves empty fields. Replace and split require nonempty search strings. `join(parts, separator)` joins a `str[]` and yields an empty string for an empty list.
- Text matching uses exact UTF-8 bytes without normalization and includes embedded NUL. `lowerAscii` and `upperAscii` only map ASCII letters. Text and list results follow the existing ARC and allocation rules.
- `std:time` exports `unixMillis()` for UTC epoch milliseconds and `monotonicNanos()` for elapsed measurements within the same process. Both return `long`. `formatUtcDate(long)` returns a UTC date in `YYYY-MM-DD` and accepts years 0001 through 9999.
- `std:random` starts with seed 0. `seed(long)` resets the program-wide generator, and the same seed and call order reproduce the sequence. `nextInt(min, max)` samples both inclusive integer bounds. Inverted bounds are errors and equal bounds return that value without consuming a sample. `nextDouble()` returns a value in `[0, 1)`.

## Optional source diagnostics

`moulang check --tiki=basic`, `--tiki=perf`, `--tiki=deep`, and `--tiki=all` enable advisory source warnings. `all` enables every rule group. `--tiki-disable=CODE[,CODE...]` suppresses named rules for the current invocation. Warnings use TI codes and exit code 0 when the check succeeds. Language errors keep their ML codes and exit code 1. The runtime independently checks structural changes during `foreach`. See the [initial rules](progress/TIKI_INSPECTOR_DESIGN.md#initial-rules) and [Deep rule](progress/ARC_TIKI_INTEGRATION_DESIGN.md#deep-rule).

## Experimental fixed arrays and classes

- A local fixed integer array can be declared as int values[3] = [10, 20, 30]. The length is part of its type and must match the initializer. Arrays support indexed reads and writes, value copies, and foreach.
- A view of a fixed array borrows its storage. The array cannot be written while that view is in local scope. The borrowed binding cannot be reassigned or returned from the function.
- A class is a reference type. Assignment copies the reference. Members are private by default. Methods can use this.field or the field name. new Counter(0) creates an instance. Class objects remain allocated until process exit.
- A class whose fields all have defaults can use new ClassName(). Declare one explicit constructor with public ClassName(typedParameters) { ... }. Field defaults run before the constructor body. Fields without defaults must be initialized directly in the first statements of the constructor.
- class Child : Parent provides single inheritance. The base default constructor or public zero-argument constructor runs first. A child reference can be assigned to the base type. Access to private base members, member hiding, method overriding, and multiple inheritance are compile errors.

The final fixed array notation and general lifetime and aliasing rules are open. Exception handling is planned for invalid conversions. Multiple inheritance is a future design in which method conflicts must be resolved explicitly. Base selection syntax and layout remain open.
