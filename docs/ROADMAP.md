# Development roadmap

1. Native Windows x64 compilation, string output, and basic VS Code integration.
2. Local variables, static type checks, operator precedence, arithmetic, and numeric output.
3. Functions, return type inference, branching, and loops.
4. Arrays and slices, structs, classes, constructors, imports, and file I/O.
5. Null safety, numeric conversions, exceptions, garbage collection, ownership and view checks, multiple inheritance, generics, and low-level memory features.

Some phases have partial implementations. See the [implementation status](IMPLEMENTATION_STATUS.md) for what is available. Further tooling work includes benchmarks, a VS Code language server, and a Windows compiler installer.

## Stage 9: memory model proposal

The current runtime allocates heap slice buffers and class objects without general release logic. Repeated append/new operations can therefore grow memory until process exit. readText also returns allocated string storage without a language-level reclamation mechanism. Temporary console conversion buffers are a separate case and are explicitly freed. Fixed-array local-view checks do not provide general heap ownership, destruction or cycle handling. These are resource-lifetime limitations, not a claim that all existing bounds/null checks are absent.

| Candidate | Ease of use | Runtime cost and release timing | Main difficulty | Native engine boundary |
| --- | --- | --- | --- | --- |
| Ownership and borrow checking | More annotations/restrictions for aliasing | Potentially low overhead; predictable destruction | Lifetimes, escaping views, diagnostics and foreign aliases | Explicit ownership transfer and scoped borrows |
| Reference counting | Automatic ordinary lifetime management | Retain/release overhead; prompt acyclic release | Cycles, weak references, copy/update correctness | Explicit retain/release handles and borrowed arguments |
| Tracing GC | Simple shared/cyclic object use | Heap tracing, roots and possible pauses; release timing varies | Root tracking, native references and scheduling | Registered roots/handles and explicit foreign-resource cleanup |

Recommended direction, pending a separate design approval: automatic reference counting for class objects and shared slice/string backing storage, with explicit weak references and clear borrowed engine handles. Keep value structs and fixed arrays as values and extend existing local-view checks conservatively. This aims to make routine use easier than C++ while retaining native compilation; "faster than Python" remains a performance goal requiring benchmarks, not a demonstrated guarantee. ARC also has costs and does not reclaim strong cycles by itself. Do not present it as a completed collector.

Before implementation, decide reference versus value semantics for mutable shared buffers, how append detaches or shares storage, destruction order/reentrancy, weak-reference syntax, cycle policy, concurrency/atomic counts and allocation failure behavior. A cycle collector can be evaluated later against measurements; do not silently add one. Unique storage can optimize away reference operations only after aliasing is proven.

For future Godot integration, use a small C ABI adapter with explicit owned/borrowed handles, release functions and documented thread/lifetime rules. Never equate Moulang object layout with Godot object layout or pass C++ standard-library objects across the boundary. Godot's native extension mechanism loads shared libraries through its extension interface; the current executable-only compiler does not yet produce that integration. See [Godot's official GDExtension overview](https://docs.godotengine.org/en/stable/engine_details/engine_api/gdextension/what_is_gdextension.html). Select the Godot version and binding contract explicitly before implementation.

Suggested milestones: instrument live allocations; specify ownership/ARC and FFI contracts; add retain/release code generation and runtime support; verify aliasing and escaping views; then prototype a native-engine adapter. Future tests must cover repeated append/new/readText loops, live-allocation baselines, copies, nullable references, closures if introduced, weak cycles, destruction order, engine-owned objects, thread restrictions and shutdown. Benchmark allocation throughput, pause times and peak/resident memory before performance claims.

This stage changes documents only. No object freeing, reference counter, borrow system extension, collector or Godot binding was implemented. Validation is an allocation-site review and documentation-link check; leak reclamation and GDExtension execution tests cannot pass until implementations exist.
