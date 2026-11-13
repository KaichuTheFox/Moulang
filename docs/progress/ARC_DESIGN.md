# Stage 6 ARC design

The owner approved alternative A. The implementation covers UTF-8 string storage, `str[]`, and child references in `str[][]`. The starting measurements are in the [pre-ARC benchmark report](PRE_ARC_BASELINE.md). The existing source syntax, evaluation order, and output rules define compatibility.

## Alternatives

| Alternative | Lifetime model | Work and risk | Expected cost |
| --- | --- | --- | --- |
| A. Owning storage slots and explicit temporaries | Code generation emits retain and release at ownership boundaries. Calls borrow their inputs and return owning values. | Requires cleanup across control flow and correct copies of aggregates. The ownership of each slot and temporary can be audited locally. | Counts are updated at each owning copy. Function arguments and provably consumed temporaries can avoid extra updates. |
| B. Borrow propagation through expressions | Reads stay borrowed wherever data-flow analysis can prove the owner survives. Copies retain when a reference escapes or the proof ends. | Requires more alias and lifetime analysis up front. Calls, aliased mutation, loop edges, and aggregate fields complicate the proof. | Can reduce counter traffic. Analysis increases compiler work, and a missed invalidation can cause a use-after-free. |
| C. Tracing collection | A collector walks explicit roots and follows managed fields. It reclaims cycles. | Requires a root registration strategy, traversal metadata, and decisions about collection timing. It is a larger change to the emitter and runtime. | Fewer updates on reference copies, with traversal costs and variable release timing. |

**Recommendation is A with typed ownership boundaries.** Start with explicit owners and validate them at every optimization level. Consider B later using measured ARC costs and tests that cover mutation through aliases.

## Object layout and runtime operations

- Dynamic strings use a heap header containing a strong reference count, byte capacity, and allocation charge, followed by immutable UTF-8 bytes. A string value carries a byte pointer, length, and owner pointer. Literals and the empty string use immutable static bytes with a null owner. A copied slice can own its own buffer. A contiguous slice or indexed code point carries its original owner even when its pointer addresses the middle of that buffer.
- Each list has a stable heap header with strong count, depth, length, capacity, and slot storage. Flat slots own string handles. Nested slots own child list pointers. Moving or sorting slots transfers existing ownership. A list resize moves the backing slots while keeping the list header stable for all aliases.
- Add internal allocation and destruction routines for string buffers and list objects, plus retain and release operations for each handle type. A newly allocated object begins with count one. Null and static handles are no-ops. Release at count zero frees the object and releases its child references. Check count and allocation arithmetic before updating state. The first implementation uses non-atomic counts for the current single-threaded language runtime.
- Update the LLVM aggregate layout and runtime C ABI together, including input, file reads, string operations, list items, and formatted output. Keep Windows APIs at the byte pointer and length boundary. Build and package the compiler and matching runtime together because internal symbols and representations change.

## Ownership and evaluation

- Owning locals, fields, and list slots retain incoming shared values before releasing their previous contents. A newly owned value can transfer its reference into a slot when the compiler proves it has been consumed. Assignment through an alias, self-assignment, and replacement with an existing child must keep valid references.
- Arguments are borrowed from owning caller values. The caller protects the receiver and arguments across argument evaluation and the call. A callee that stores an argument or returns it acquires an owning reference. Return slots transfer an owned value to the caller before callee cleanup.
- Emit cleanup for initialized managed slots on block exit, return, break, and continue. Short-circuit expressions and branches release only values initialized on the taken path. Keep temporaries alive until the enclosing operation finishes, including later argument evaluation that can mutate a list. Existing fatal runtime errors end the process. Debug shutdown records those failures separately from normal-exit leak results.
- Preserve shared list identity. An indexed read protects the returned string or child through later mutation. Pop transfers ownership of the removed item to its result. Remove releases the removed item. Insert, replacement, and resize must leave a valid list when an allocation or validation fails.
- Copy and destroy value structs recursively where they contain managed fields. Nullable managed values acquire and release their payload only when present. Existing class instances have process lifetime and act as roots for owned fields. Reassigning such a field releases its prior reference. Track the fields owned by those roots separately in debug reports. Do not treat the continued existence of a class root as a leaked string or list.

## Cycles and memory budget

Current nested-list types form a descending depth graph. A `str[][]` can own `str[]` rows, and a `str[]` can own strings. Neither can store a reference back to its parent or to a class. Shared rows therefore form an acyclic graph even when they appear in multiple slots. Keep that type restriction for the initial rollout.

Weak references could represent non-owning back edges when classes gain ARC, but they would require an upgrade operation and rules for expired references. A separate cycle collector could reclaim strong cycles at the cost of traversal, barriers, or scheduling work. Restricting strong edges is the simplest current option. Reassess weak references or cycle collection when a future ownership graph permits cycles.

Charge live bytes for the allocation families currently covered by the 256 MiB budget. Include the managed header and current slot capacity, subtract charges after free, and reserve a resize delta before allocating. Keep the old allocation and charge if growth fails. This accounting covers the designated allocations and does not measure process working set. `RText()` buffers can gain owners while preserving their existing size limit and separate accounting; including them in the 256 MiB budget would change existing file behavior. Track their live bytes in debug counters.

## Debug checks

- Build a separate instrumented runtime for memory tests. Its registry records object kind, size, allocation ID, and outstanding counts. Report live object totals and bytes at normal exit, distinguishing retained class roots from objects left unaccounted for.
- Poison released payload bytes, quarantine a bounded number of allocations, and validate ownership handles before checked runtime accesses. Pair debug records with allocation generations so pointer reuse cannot make a stale handle appear live. Bound quarantine bytes separately from live allocation accounting. Test a deliberate stale-handle path in a runtime fixture. These checks cover instrumented accesses; arbitrary uninstrumented reads require other tools.
- Compile the registry, poison, quarantine, and checked-handle code out of the production runtime. Verify production imports, symbols, and representative optimized IR for absence of the debug paths. Benchmark production ARC separately from debug instrumentation.
- Evaluate AddressSanitizer for both generated programs and runtime code with the Windows toolchain. Report the covered execution paths if it can be linked and run.

The Debug runtime uses monotonically increasing allocation IDs as opaque handles. It validates the ID and kind before accessing managed storage and checks owned string ranges. Quarantine retains up to 64 allocations of at most 16 KiB each. Set `MOULANG_ARC_REPORT=1` to report remaining objects and bytes on standard error after normal completion. The report counts objects recovered from class fields separately from residual live objects. Fatal runtime errors report that normal leak reporting was skipped. Release builds use direct heap handles.

## Rollout and verification

1. Establish a runtime test harness for reference transitions, live-byte accounting, allocation errors, and debug reporting. Define the LLVM and runtime ABI before changing either side.
2. Add string owners, literal handling, and slice lifetime tests. Cover all sources of allocated strings and every store, return, and cleanup path that can hold them.
3. Add flat and nested list owners, slot transfers, structs, nullable values, and class field roots. Exercise mutation through multiple aliases and passing a child out of a parent that is then released.
4. Run a million bounded-size concatenations and repeated list mutation, plus tests for self-assignment, an escaping string slice, a surviving nested row, the 256 MiB limit, leak reports, and deliberate invalid accesses. Bound input size independently so the million-iteration test measures reclamation without requiring unbounded string growth.
5. Run Debug and Release regressions and the same benchmark inputs. At -O0, explicit retain/release calls and cleanup blocks are likely most visible. At -O2 and -O3, LLVM may simplify control flow and inline visible code, while calls to separately compiled runtime routines generally still need explicit ownership-aware optimization. Measure time, peak working set, and retained bytes before judging any level.

The main implementation risks are missing ownership edges in aggregate or error paths, freeing storage while another alias can observe it, and counter traffic in small loops. A staged rollout with lifetime fixtures is expected to take more effort in code generation and ABI verification than in the counter implementation. Performance outcomes and implementation time are estimates until the approved prototype runs.
