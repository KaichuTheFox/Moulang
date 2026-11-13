# Stage 13 module interfaces

Status is approved under policy A.

## Current compilation model

Relative imports load source files once, detect cycles, and resolve public declarations across the reachable source graph. Top-level declarations are public by default. `private` hides an implementation detail, and the semantic analyzer checks public signatures for exposed private types. The compiler emits one LLVM module and one COFF object for the complete program. User functions other than the entry point have internal LLVM linkage and symbols that depend on their position in the source graph. The compiler links that object with the bundled runtime into an executable.

A Windows x64 probe imported a public function from a `.mcx` file and returned 42. A call to its private helper reported ML3010. `check --library` accepted the provider without a `main`, while `build` reported ML3001 for the same provider. An angle-bracket import reported ML2001. See the [source import rules](../LANGUAGE_SPEC.md#imports-and-file-io) and [compiler architecture](../COMPILER_ARCHITECTURE.md).

## A. Keep source imports as the module interface

Use the existing `import "modules/math.mcx"`, or another supported source suffix. Declare intended public signatures explicitly and mark implementation helpers private. This covers public API visibility and source-distributed libraries. The compiler can analyze and optimize the complete source graph as one module.

The Stage 12 source suffixes are `.mcx`, `.m`, `.mxx`, `.mpp`, `.mcp`, `.m++`, and `.mc+`. They can be mixed in one source graph and share the same import and visibility rules.

Build caching can be designed independently of headers. A first cache can reuse a complete result for an identical source graph, using file contents and diagnostic path identities, compiler and runtime identity, target, and build flags as inputs. Verify dependency edits and configuration changes invalidate the cache before measuring speedup.

Use A for projects that can ship their source. A separately linked binary library would need the compiler, linker, and artifact work in B.

## Future option B. Generate a typed interface and a static library

If a library must ship without source, generate both its interface metadata and static archive from one checked compilation. A possible import form is shown below.

```m
import <math.mci> as math

main() {
    print(math.twice(21))
}
```

- Publish `math.mci` with `math.lib`. Treat the interface as a versioned declaration artifact. The importer type-checks its declarations and the linker resolves their symbols. The archive carries the implementation. This permits separate compilation and distribution of machine code without the implementation source files.
- Select the public exports in the library build. Derive each name, canonical parameter and return type, and stable linker symbol from the checked source. Keep the API qualified by a file-local alias. Resolve library paths from an explicit project library root. Quoted source imports and `std:` imports keep their current meaning.
- Start with standalone functions using scalar integer and real types and `void` on the current Windows x64 target. Define a stable ABI revision and calling convention first. Managed strings and lists, class handles, nullable payloads, and by-value structures require layout, ownership, and version rules before export. A consumer links one compatible Moulang runtime.
- Record the interface format version, runtime ABI, target triple, export signatures, dependency identities, and a digest of the matching archive. Reject missing archives, stale pairs, unsupported versions or targets, duplicate exports, and mismatched signatures with diagnostics at the import. Validate the provider's export set at packaging and its archive at link preparation.
- Keep source and library dependency graphs acyclic. The first library form can depend on its own bundled source helpers and the existing runtime. Defer package-to-package binary dependencies until their identity, cycle, and link-order rules are specified. Keep private implementation symbols out of the importer's declaration set.
- Cache the provider object and interface. A private implementation edit can rebuild the provider and relink without redoing consumer type checking if the public interface is unchanged. Changes to exported types, compiler/runtime ABI, or the target invalidate affected outputs. Separating object files can limit the cross-file optimization that a single LLVM module currently permits. Compare cold builds, warm rebuilds, private implementation edits, API edits, and generated execution performance before choosing a cache design.

The interface contains typed declarations. A source importer continues to parse a separate module and resolve symbols rather than inserting interface text into the consumer.

## Interface suffixes if B is selected

- **`.mh+`** follows the original header-style suggestion. Treat `+` as a literal in paths and escape it in regular expressions or filter engines where it is special.
- **`.mhi`** is a letter-only interface suffix. The header-style name can suggest a manually maintained source header.
- **`.mci`** is a letter-only candidate for a generated Moulang compiled interface. Use it if the artifact is introduced. The paired native library retains its platform suffix.

Interface suffixes would use a separate import rule. The seven Stage 12 source suffixes and existing source associations remain available.

## Decision and regression coverage

The owner approved A. Stage 13 validates the existing source module behavior. Binary interfaces remain on the [roadmap](../ROADMAP.md#source-module-interfaces) for source-free distribution and independently linked libraries.

The source extension integration test now covers transitive public calls across `.mcx` and `.m`, file-local private functions and types, private types exposed through public signatures, public function and type name conflicts, and a three-file cycle spanning `.mcx`, `.m`, and `.mcp`. The VS Code suite runs a `.mcx` entry importing a legacy `.m` module.

Release passed 25 CTest entries and Debug passed 22. The CTest fixtures confirmed both ordinary print and block formatting with imported function results. VS Code unit tests and the isolated extension source and installed VSIX suites passed.

Stage 13 adds regression coverage in `tests/integration/source-extensions.ps1` and `tests/vscode/suite.js`. The specification, implementation status, roadmap, compiler architecture, and documentation index record the existing behavior and decision.

If B is pursued later, validate a library with its source absent, API signature and archive mismatches, separate optimization levels, legacy and standard imports, and the existing CTest, VS Code, and relocation suites. Compare cold and incremental build times before claiming a performance improvement.
