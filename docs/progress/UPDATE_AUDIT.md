# Moulang update audit

The starting checkout was clean on main at bfe9db5. CMake reports compiler 0.3.0 and the extension manifest reports 0.1.6. The tagged v0.3.0 Windows workflow completed its build, installation verification, and GitHub Release jobs. Its published ZIP and checksum were downloaded and verified during the release. Restricted tool discovery was tested on Windows; a clean Windows 10 or 11 VM was not used.

The Stage 1 rerun configured and built both compiler configurations. CTest passed 16 of 16 Release and 13 of 13 Debug tests. All eight VS Code unit tests and the repository link test passed. VS Code 1.144.0 passed both the isolated source and installed VSIX suites.

| Status | Findings |
| --- | --- |
| Implemented | UTF-8 validation, scalar and collection type checks, nullable flow checks, checked indexing, fixed-array view restrictions, and source-located ML errors. |
| Partially implemented | Heap storage remains allocated through process exit. New dynamic string buffers and string list headers and slot capacities share a 256 MiB limit. Earlier integer slice, class, and file-read allocations use separate paths. |
| Partially implemented | Integer slices store a buffer and length. Standalone append returns an extended slice without changing the old slice's visible length or elements. String lists share mutable identity across assignment and calls. Nested lists share child objects. |
| Partially implemented | String indexing counts UTF-8 code points and walks to the target byte offset. Even an ASCII index near the start first counts the whole string, so a lookup takes O(n) bytes and a loop indexing every point can take O(n²). The current foreach supports integer collections. |
| Partially implemented | Built-ins cover printing and print blocks, type queries, numeric conversions, integer append, string-list length and mutations, and text read, replace, and append. Math, time, random, and general string utilities still need a library design. |
| Unimplemented | ARC reclamation, general leak and use-after-free diagnostics, Tiki Inspector, and configurable warning rules. Existing semantic errors and runtime checks use separate code paths. |
| Design required | Conversion consistency, list/slice consolidation, and additional static-analysis rules. |

Source loading and semantic analysis precede LLVM C API lowering. The backend verifies LLVM IR and emits a Windows x64 GNU ABI COFF object. The driver links it with the static runtime. Build defaults to -O0, run uses -O0, and -O2 and -O3 select the corresponding LLVM default pipeline followed by verification. CMake Release and Debug describe how the compiler itself is built.

The ten source snippets and exact diagnostic strings in [Diagnostic proposals](../DIAGNOSTIC_PROPOSALS.md) were re-run through compiler 0.3.0. All ten matched. That document identifies concrete improvements to source spans, type explanations, name suggestions, and hints while preserving machine-readable ML diagnostics.

## Numeric conversion matrix

Rows are sources and columns are destinations. Each identity and implicit conversion is also available as an explicit cast. The matrix describes values of the listed static types.

| From | char (u8) | short (i16) | int (i32) | long (i64) | float (f32) | double (f64) |
| --- | --- | --- | --- | --- | --- | --- |
| char | Identity | Implicit zero extension | Implicit zero extension | Implicit zero extension | Implicit exact | Implicit exact |
| short | Explicit low 8 bits | Identity | Implicit sign extension | Implicit sign extension | Implicit exact | Implicit exact |
| int | Explicit low 8 bits | Explicit low 16 bits | Identity | Implicit sign extension | Explicit, may round | Implicit exact |
| long | Explicit low 8 bits | Explicit low 16 bits | Explicit, signed range checked | Identity | Explicit, may round | Explicit, may round |
| float | Explicit rounded then low 8 bits | Explicit rounded then low 16 bits | Explicit rounded then low 32 bits | Explicit rounded then low 64 bits | Identity | Implicit real widening |
| double | Explicit rounded then low 8 bits | Explicit rounded then low 16 bits | Explicit rounded then low 32 bits | Explicit rounded then low 64 bits | Explicit, may round, finite overflow checked | Identity |

Truncated bits are interpreted in the destination type, so char is unsigned while short, int, and long are signed. Real-to-integer rounds to the nearest integer with ties to even and preserves the stated low bits even for finite out-of-range values. Non-finite real-to-integer conversions fail. Double-to-float rejects finite magnitudes above the largest float and allows non-finite IEEE values. An exactly representable real literal may initialize or be assigned to float in a typed context. An exactly representable integer literal can also initialize a positional float field. Mixed long and real expressions require an explicit conversion.

## Documentation, CI, and Git privacy

- The [documentation index](../README.md) links the language, deployment, and privacy guides. The migration snippets were checked with the 0.3.0 compiler. The old spelling produced ML2002 and the new spelling passed. All top-level examples ran successfully.
- The PE import and archive verification tests passed under Release and Debug. Local packaging scanned the project files, VSIX entries, and vendor linker, then wrote a new ZIP and checksum. The extracted package passed its file hashes, notice checks, and Hello World with restricted tool discovery. The revised workflow still requires a hosted run. See [release verification](../RELEASE.md).
- The remote Git refs matched the local main and both tags. The read-only email counts and owner checklist are recorded in [Privacy](../PRIVACY.md).

## Pre-ARC baseline and next decision

Thirty supported workload and language combinations completed a warm-up and five measured runs, each with verified output. Six combinations are marked N/A. See the [benchmark report](PRE_ARC_BASELINE.md), [CSV](PRE_ARC_BASELINE.csv), and [reproduction instructions](../../tests/benchmarks/README.md).

The [ARC design](ARC_DESIGN.md) compares three alternatives. The owner approved owning slots with explicit retain and release for Stage 6. The implementation and the before-and-after measurements are recorded in [ARC validation](ARC_VALIDATION.md).
