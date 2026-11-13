# Compiler architecture

```text
.mcx and compatible source suffixes -> Source Manager -> Lexer -> Parser -> AST -> Semantic Analyzer
   -> LLVM IR -> LLVM X86 TargetMachine -> COFF .obj
   -> ld.lld + static Moulang/LLVM-MinGW runtime and Windows imports -> x64 .exe
```

- src/source validates UTF-8 and resolves imports. src/lexer and src/parser produce tokens and an AST with source locations. src/semantic checks the entry point, signatures, source visibility, local scopes, return paths, and nullable values. The [language specification](LANGUAGE_SPEC.md#imports-and-file-io) lists the supported source suffixes and import rules.
- src/codegen/llvm uses the LLVM C API to declare functions and lower their bodies to basic blocks. It verifies the module, runs the selected optimization pipeline for -O2 or -O3, and emits an x86_64-w64-windows-gnu COFF object compatible with the separately built static runtime.
- src/driver links in a temporary directory. build writes the result after successful linking. run launches the temporary executable and forwards its exit code.
- runtime/src implements startup, output, checked integer division, slice buffers, and other runtime operations. It writes UTF-16 to the Windows console and UTF-8 to redirected output.

Compiler diagnostics use the format file:line:column: error MLxxxx: message. Lines and columns are one-based. Columns count Unicode code points.
