# Moulang user guide

Moulang is a statically typed native language developed by HAZOS. It currently targets Windows 10/11 x64. Follow the [source build instructions](../README.md#build-from-source) with Visual Studio, the Windows SDK, CMake, Ninja, and the pinned toolchains, or use a verified release package.

## Run a program

Open the repository root in PowerShell. Add the compiler directory to `PATH`, or use the full path to the executable. Start with [Hello World](../examples/hello.mcx) on a current source build. The published 0.3.0 preview includes [the legacy example](../examples/hello.m).

```powershell
moulang run examples/hello.mcx
moulang build examples/hello.mcx -o build/moulang/hello.exe
.\build\moulang\hello.exe
moulang check examples/hello.mcx
```

Install the VSIX using **Extensions: Install from VSIX**. Open the folder containing the source file. Set **Moulang: Executable Path** (`moulang.executablePath`) if the compiler is outside `PATH`. Open a saved Moulang source file and click the editor's play button, or use **Moulang: Run Current File** from the Command Palette.

## Choose a source extension

Use `.mcx` for new files. The compiler and VS Code extension also support `.m`, `.mxx`, `.mpp`, `.mcp`, `.m++`, and `.mc+`. They all use the same language. A `.mcx` entry can import a legacy file with `import "modules/format.m"`, or a plus-suffix file with `import "modules/helper.mc+"`.

Existing `.m` projects can keep their filenames. To migrate a file, update imports and scripts that name it and run `moulang check` again. Use lowercase suffixes in imports. Windows may resolve a lowercase path to an uppercase filename in a case-insensitive directory.

## Learn by running the examples

- [Variables](../examples/variables.m) shows explicit types, `var` inference, arithmetic, and reassignment. [Numbers](../examples/numbers.m) shows numeric types and conversions.
- [Text](../examples/text.m) shows the current source build's `str` declarations, code point slices, multi-argument print, and static type queries. Use the [print block example](LANGUAGE_SPEC.md#print-blocks) to try formatted prompts and typed line input.
- [String lists](../examples/lists.m) shows list methods, shared rows, ragged tables, and post-mutation blocks in the current source build.
- [Control flow](../examples/control.m) shows functions, return values, branches, and loops.
- [Slices and null](../examples/slices-and-null.m) shows integer slices, `append`, and nullable values. [Advanced examples](../examples/advanced.m) show slice views and `foreach`. The [language specification](LANGUAGE_SPEC.md#experimental-fixed-arrays-and-classes) describes local fixed integer arrays.
- [Structs](../examples/structs.m) demonstrates value copies and fields. [Objects](../examples/objects.m) demonstrates classes, constructors, and single inheritance.
- [Advanced examples](../examples/advanced.m) also show a relative import from [a local module](../examples/modules/format.m).
- [Standard modules](../examples/standard-library.m) show qualified math, text, time, and random calls in current source builds.

## Use standard modules

```m
import "std:math"
import "std:text" as strings

main() {
    print(math.sqrt(81.0))
    print(strings.join(strings.split("a,b", ","), " + "))
}
```

This prints `9` and `a + b`. Import an alias in each file that uses it. The VS Code extension suggests module functions after the alias and a dot. See the [module reference](LANGUAGE_SPEC.md#standard-modules) for API details, including code-point indices, UTC dates, and repeatable random seeds.

## Migrate from Moulang 0.2.0

Compiler 0.3.0 reports ML2002 when it encounters the old `string` type spelling. Update type annotations in variables, parameters, return types, and fields to `str`.

Before

```m
string name = "Moulang"
```

After

```m
str name = "Moulang"
```

The quoted value keeps the same UTF-8 bytes.

## Check managed memory in a Debug build

Build the compiler and its matching Debug runtime together. Set `MOULANG_ARC_REPORT=1` before running a program with that compiler to print a memory report on standard error. The report gives remaining managed objects and bytes, and separately counts memory released from class fields during shutdown. A runtime error can end execution before a normal leak report.

```powershell
./tools/Build-Windows.ps1 -Configuration Debug -BuildDirectory build/windows-debug
$env:MOULANG_ARC_REPORT = '1'
./build/windows-debug/bin/moulang.exe run examples/lists.m
```

Existing sources use the same syntax. Rebuild with the matching compiler and runtime archive to use the new lifetime behavior.

## Iterate over a string

```m
main() {
    str greeting = "Hello"
    foreach (letter in greeting) {
        print(letter)
    }
}
```

Each value is one Unicode code point as a `str`. The loop reads its input once and advances through the UTF-8 bytes in order.

Use `text[0]` for the first code point and `text[-1]` for the last. Both take constant time, including on mixed UTF-8 text. All ASCII indices take constant time. On other strings, access takes time proportional to the distance from the selected end. Use `foreach` for a sequential scan through all code points.

## Iterate over a string list

```m
main() {
    str[] names = ["Ada", "Grace"]
    var shared = names
    foreach (name in names) {
        if (name == "Ada") { shared[1] = "Lin" }
        print(name)
    }
    shared.append("Grace")
}
```

This prints `Ada` and then `Lin`. Replacing a slot keeps the list's structure, so a later iteration reads the replacement. Add, remove, or reorder elements after the loop finishes. A structural change through any alias during the loop reports an error at the operation. A loop over `str[][]` yields one shared `str[]` row at a time.

## Choose an integer list or slice

```m
main() {
    int[] numbers = [3, 1, 2]
    var original = numbers[0:3]
    numbers.sort()
    numbers.append(4)
    print(numbers, original[0])
}
```

This prints `[1, 2, 3, 4] 3`. The prefix spelling `int[] numbers` creates a shared list. The suffix spelling `int numbers[]` creates the existing integer slice and still works with `numbers = append(numbers, 4)`.

To migrate a slice while keeping its contents independent, declare a new `int[]` and append each value with foreach. Use one list variable when mutations should be shared by its aliases.

## Migrate integer narrowing casts

Converting a wider integer to `char`, `short`, or `int` now checks the destination range. For example, `char(260)` reports a runtime error. Validate input before casting when the program needs to handle invalid values itself.

When wrapping is intentional, select the low bits before casting. Mask with 255 for unsigned `char`. For signed `short`, subtract the masked sign bit from the lower 15 bits.

```m
main() {
    int raw = 260
    print(char(raw & 255))
    long bits = 65535
    print(short((bits & 32767) - (bits & 32768)))
}
```

The example prints `4` and `-1`. The resulting values fit their destination types.

## Run Tiki Inspector

Tiki Inspector adds optional warnings to a successful source check. Run a mode for the rules you want.

```powershell
moulang check main.mcx --tiki=basic
moulang check main.mcx --tiki=perf
moulang check main.mcx --tiki=deep
moulang check main.mcx --tiki=all --tiki-disable=TI-D001
```

Basic reports unused local variables. Performance reports repeated string accumulation and a known non-ASCII string traversed with repeated indexing. The compiler prints each warning with its code, location, reason, and hint. Warnings alone return exit code 0.

Deep reports `TI-D001` when `append`, `insert`, `remove`, or `pop` targets a known alias of a list being traversed by `foreach`. If you intend to visit the original items, you can collect additions in a separate list and apply them after the loop.

```moulang
main() {
    int[] values = [1, 2]
    int[] additions = []
    foreach (value in values) { additions.append(value * 2) }
    foreach (value in additions) { values.append(value) }
    print(values)
}
```

Deep tracks direct list bindings within one function. Aliasing through calls, fields, and nested list slots can escape this analysis. The runtime still checks structural changes during iteration. See the [rule and its coverage](progress/ARC_TIKI_INTEGRATION_DESIGN.md#deep-rule).

In a trusted VS Code workspace, enable **Moulang: Live Diagnostics**, then set **Moulang: Tiki Mode** to `basic`, `perf`, `deep`, or `all`. Warnings from unsaved edits appear in **Problems**. The default is `off`. Use a compiler built with the selected Tiki mode.

## Read diagnostics

A compiler diagnostic such as `hello.m:3:11: error ML3004: ...` identifies a source file, line, column, and diagnostic code. Fix the first reported issue and run `moulang check` again. In VS Code, select a diagnostic in **Problems** to jump to the source. Enable **Moulang: Live Diagnostics** in a trusted workspace to check edits before saving.

Interactive terminals also show the source line and a caret. Common errors include an explanation or hint, such as a uniquely matching name in scope. Redirected standard error uses the single-line format. Pass `--diagnostic-format=rich` to include source context in redirected output, or `--diagnostic-format=plain` to keep terminal output to one line. Both options work with `check`, `build`, and `run`.

```text
example.m:1:16: error ML3004: Undeclared variable 'missing'.
  1 | main() { print(missing) }
    |                ^~~~~~~
  hint: Declare the variable in a visible scope or check its spelling.
```

The first line's column counts Unicode code points. The displayed excerpt expands tabs at four-column stops, shows precomposed Hangul syllables in two cells, and escapes other non-ASCII code points and control characters so the caret can follow the displayed text. The compiler uses the unsaved input snapshot for `check --stdin` when rich output is requested.

If the run button is missing, open a saved `.m` file and verify its language mode is Moulang. If the compiler is missing, update `moulang.executablePath` and keep `moulang_runtime.lib` beside the executable. See the [README troubleshooting section](../README.md#troubleshooting) for toolchain issues.

See the [language specification](LANGUAGE_SPEC.md) for syntax and the [implementation status](IMPLEMENTATION_STATUS.md) for current limitations and planned features.
