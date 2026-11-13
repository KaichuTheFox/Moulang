# Stage 9 integer slices and lists

Status: policy A approved and implemented.

## Behavior before Stage 9

- `int values[] = [1, 2]` and a bare integer literal such as `var values = [1, 2]` create an integer slice. Untyped `[]` also creates an integer slice. `append(values, 3)` returns an extended slice and preserves the original length and visible elements. An unbranched append can reuse spare buffer capacity. Heap-backed integer slice buffers currently live until process exit.
- `str[]` is a shared mutable list managed by ARC. Assignment and argument passing share its identity. Indexed replacement is allowed during foreach. Structural mutation through any alias is rejected while an iterator is active.
- `int values[N]` is a fixed-size value array. Slicing a fixed array creates a read-only borrow that prevents array writes while in scope. Slicing an integer slice creates a read-only view of its existing contents.

## A. Introduce an explicit shared integer list

Add the prefix spelling `int[] values = [1, 2]` for the mutable reference list. Contextually typed literals, including `[]`, take the requested list type. Keep `int values[]`, bare integer literal inference, fixed arrays, and the standalone slice append compatible. An assignment or function call involving an explicitly typed list shares its identity. Conversion from a slice to a list is an explicit copy. Existing code can migrate by filling a typed list with foreach and append.

```m
main() {
    int[] numbers = [10, 20, 30]
    var alias = numbers
    alias.append(40)
    numbers.remove(20)
    numbers.sort()
}
```

The proposed list methods are `append`, `insert`, `remove`, `pop`, and `sort`. Use `len(numbers)` for the length. Use the string list rules for index bounds, negative indices, and structural changes during foreach. Removal finds the first equal element. Sort uses ascending signed integer order. `pop` returns the removed integer. A list view uses the existing half-open nonnegative range syntax and reads a stable snapshot. Creating the view retains the backing buffer and uses constant additional space. Before a write, the list copies the buffer if a view pins it. This keeps views stable across writes, growth, and release of the list. Slicing a fixed array keeps its existing borrow rule.

Use ARC for list headers and buffers. Pack integers into 32-bit slots. Share list growth, bounds, and iterator rules where the existing runtime representation permits. A separate integer element path avoids string-sized records for numeric data. Indexing is constant time. Append can be amortized constant time with capacity growth and an unpinned buffer. A write that detaches a viewed buffer takes linear time. Legacy slices retain their current allocation behavior in this stage.

## B. Use one mutable dynamic integer collection

Make `int values[]`, `int[] values`, and inferred integer literals produce the same shared list. Keep standalone `append(values, item)` as an independent-result compatibility operation. It must return an extended list whose future edits are independent of the input. A straightforward implementation copies on each call. A shared-buffer implementation can defer the copy until an edit needs it. Either approach needs explicit regression tests for append-heavy programs and aliases. Fixed arrays stay value types and views retain the pre-write buffer as in A.

B gives users one dynamic integer collection and allows ARC to reclaim buffers from legacy syntax. It changes how existing declarations, type inference, and calls map to the runtime. Branching append, imports and function signatures, empty literals, and the cost of repeated standalone append all need compatibility checks before rollout.

## Decision

Policy A was approved for Stage 9. New code gets the same mutation and alias rules as string lists. Existing programs keep their slice behavior and append cost. Revisit a legacy migration once the shared list and snapshot view behavior have been measured.

Start with `int[]`. For later homogeneous list types, use element-aware storage and copy/release rules. `bool[]` and `char[]` can use compact slots. `double[]` needs a policy for NaN in removal and sorting. Struct lists copy each struct value while retaining its managed fields. Mixed-element lists need their own type rules.

## Validation

Release passed 22 CTest entries and Debug passed 19. The tests exercised legacy branch-append, nested views and fixed-array borrows, as well as typed empty literals, negative indices, sorting, shared mutation, and guarded foreach. Views kept their original values across replacement, reorder, growth, and release of their lists. The Debug run reported zero remaining managed objects. VS Code unit tests and the isolated source and installed VSIX tests passed.

## Performance

Windows x64 on a Ryzen 5 5600X. Each case uses the current Release compiler and matching runtime, with generated code built at `-O2`. The results are medians of five runs after one warm-up and include process startup and input setup.

- Appending and reading one million integers took 73.887 ms with a legacy slice and 67.191 ms with a shared list. Median peak working set was 12.62 MiB and 11.01 MiB, respectively.
- Building a 262,144-element list and writing its first element 128 times took 61.824 ms. Creating and checking a fresh snapshot for each write took 93.276 ms. That second case includes the buffer copy and the snapshot check, so the difference measures the whole workload.

See the [benchmark report](INTEGER_LIST_BENCHMARK.md) and [raw medians](INTEGER_LIST_BENCHMARK.csv).

## Changed files

- Types, parser and semantic checks in `include/moulang/types.hpp`, `src/parser/parser.cpp`, and `src/semantic/semantic.cpp`.
- LLVM emission and list runtime in `src/codegen/llvm/object.cpp` and `runtime/src/runtime.cpp`.
- Test registration and fixtures in `CMakeLists.txt`, `tests/integration/int-lists.ps1`, and `tests/vscode/suite.js`.
- Benchmark fixture, runner, and instructions in `tests/benchmarks/int-list.m`, `tests/benchmarks/run.py`, and `tests/benchmarks/README.md`.
- Usage and status in `README.md`, `CHANGELOG.md`, `docs/README.md`, `docs/LANGUAGE_SPEC.md`, `docs/USER_GUIDE.md`, and `docs/IMPLEMENTATION_STATUS.md`.
