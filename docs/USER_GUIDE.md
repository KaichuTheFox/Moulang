# Moulang user guide

Moulang is a statically typed native language developed by HAZOS. It currently targets Windows 10/11 x64. Follow the [source build instructions](../README.md#build-from-source) with Visual Studio, the Windows SDK, CMake, Ninja, and the pinned toolchains, or use a verified release package.

## Run a program

Open the repository root in PowerShell. Add the compiler directory to `PATH`, or use the full path to the executable. Start with [Hello World](../examples/hello.m).

```powershell
moulang run examples/hello.m
moulang build examples/hello.m -o build/moulang/hello.exe
.\build\moulang\hello.exe
moulang check examples/hello.m
```

Install the VSIX using **Extensions: Install from VSIX**. Open the folder containing the source file. Set **Moulang: Executable Path** (`moulang.executablePath`) if the compiler is outside `PATH`. Open a saved `.m` file and click the editor's play button, or use **Moulang: Run Current File** from the Command Palette.

## Learn by running the examples

- [Variables](../examples/variables.m) shows explicit types, `var` inference, arithmetic, and reassignment. [Numbers](../examples/numbers.m) shows numeric types and conversions.
- [Text](../examples/text.m) shows the current source build's `str` declarations, code point slices, multi-argument print, and static type queries. Use the [print block example](LANGUAGE_SPEC.md#print-blocks) to try formatted prompts and typed line input.
- [String lists](../examples/lists.m) shows list methods, shared rows, ragged tables, and post-mutation blocks in the current source build.
- [Control flow](../examples/control.m) shows functions, return values, branches, and loops.
- [Slices and null](../examples/slices-and-null.m) shows integer slices, `append`, and nullable values. [Advanced examples](../examples/advanced.m) show slice views and `foreach`. The [language specification](LANGUAGE_SPEC.md#experimental-fixed-arrays-and-classes) describes local fixed integer arrays.
- [Structs](../examples/structs.m) demonstrates value copies and fields. [Objects](../examples/objects.m) demonstrates classes, constructors, and single inheritance.
- [Advanced examples](../examples/advanced.m) also show a relative import from [a local module](../examples/modules/format.m).

## Read diagnostics

A compiler diagnostic such as `hello.m:3:11: error ML3004: ...` identifies a source file, line, column, and diagnostic code. Fix the first reported issue and run `moulang check` again. In VS Code, select a diagnostic in **Problems** to jump to the source. Enable **Moulang: Live Diagnostics** in a trusted workspace to check edits before saving.

If the run button is missing, open a saved `.m` file and verify its language mode is Moulang. If the compiler is missing, update `moulang.executablePath` and keep `moulang_runtime.lib` beside the executable. See the [README troubleshooting section](../README.md#troubleshooting) for toolchain issues.

See the [language specification](LANGUAGE_SPEC.md) for syntax and the [implementation status](IMPLEMENTATION_STATUS.md) for current limitations and planned features.
