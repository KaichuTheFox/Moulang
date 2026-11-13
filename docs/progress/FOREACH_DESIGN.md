# Stage 8 iteration and indexing decisions

String foreach now advances a byte cursor through the validated UTF-8 input. It evaluates the source once, retains it for the loop, and exposes each code point as a one-code-point str. Each item can share the original backing allocation. This visits n bytes in O(n) time, with constant iterator storage. Any values retained by the program still contribute their own storage or keep a shared buffer alive.

The [string foreach benchmark](STRING_FOREACH_BENCHMARK.md) checks 1,200,000 code-point comparisons in both ASCII and Hangul cases. The [CSV](STRING_FOREACH_BENCHMARK.csv) records five-run medians. These sequential sweeps have different workloads from the historical indexed sweeps.

## List mutation during iteration

Approved policy A rejects structural changes to the list while an iterator is active. Append, insert, successful remove and pop, and sort that would change slot order fail at the operation. A sort of an already sorted list is allowed. Indexed assignment replaces the slot value without changing its position. The next iteration reads the current value at its slot.

The shared list header contains a structural version and an active iterator count. The iterator records the version on entry and verifies it when advancing. A structural mutator checks the active count before changing the list so an attempted change followed by an immediate break or return still fails at the operation. Every alias shares this header. Each loop needs constant space for its list reference, version, and cursor.

For nested lists, the guard applies to the list being traversed. Iterating a str[][] protects the count and order of its row slots. A child row can be changed independently if it has no active iterator. The loop owns the original list and the current element. It ends its iterator before releasing the list on normal exit, break, and return. Continue keeps the iterator active and releases the current item. ARC preserves a string or row already yielded when a slot is replaced.

The list-foreach integration test covers flat and nested lists, alias writes via functions, mutation errors at the operation, indexed replacement, nested iterators, early exits, empty lists, and normal mutation after traversal. The native Debug fixture also checks rejection of a stale iterator version.

## String indexing

Approved policy F records whether each string contains only ASCII bytes. A subscript then uses the byte length as the code-point count and the normalized index as the byte offset. Both positive and negative indexing are O(1) for ASCII. Non-ASCII strings keep the existing code-point count and offset scans. String foreach continues to use its forward cursor.

The ASCII flag occupies former padding in the native string and string list slot layouts on Windows x64. The compiler determines it for decoded literals. The runtime determines it for validated input and file text, propagates it through concatenation and repetition, and determines it for slices and yielded code points. A slice from a mixed source can itself be ASCII. The internal calling conventions changed, so use the compiler with the runtime archive from the same build.

The ASCII-index integration test covers both index directions, bounds, embedded NUL, combining marks, Hangul, emoji, slices, input and file text, list storage, and ARC lifetime after a temporary's last use.

## Measured performance

Windows x64 on a Ryzen 5 5600X. Each executable was built with `-O2` and timed five times after one warm-up. The figures are medians and include process startup, text or list setup, computation, and one captured result.

- ASCII, 16 code points and 100,000 lookups: 58.515 ms before, 60.445 ms after.
- ASCII, 16,384 code points and 16,000 lookups: 152.513 ms before, 58.788 ms after, or 2.59 times the original throughput.
- Hangul, 16 code points and 100,000 lookups: 61.389 ms before, 58.702 ms after.
- Hangul, 16,384 code points and 16,000 lookups: 281.553 ms before, 292.839 ms after. The UTF-8 search algorithm is unchanged.
- Across the string cases, median peak working set differed by at most 8 KiB. The flag fits in the existing 24-byte string and list slot layouts.
- A manual indexed traversal of 4,096 string list elements across 1,000 passes took 89.558 ms with the previous build and 93.160 ms with the updated build. Guarded foreach took 89.959 ms with the updated build. Both methods on the updated build had a median peak working set of 4,874,240 bytes.

Startup is included in the short cases, so their few-millisecond differences do not isolate indexing overhead. The list comparison uses the equivalent indexed loop as the baseline because the previous compiler did not support foreach over lists. The measurements do not isolate the ASCII detection cost of file reads, input, or slicing.

Raw measurements are available for [string indexing before](STRING_INDEX_BEFORE.csv), [string indexing after](STRING_INDEX_AFTER.csv), [list indexing before](LIST_TRAVERSAL_BEFORE.csv), [list indexing after](LIST_TRAVERSAL_INDEXED_AFTER.csv), and [list foreach](LIST_TRAVERSAL_AFTER.csv).
