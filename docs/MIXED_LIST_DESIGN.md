# Mixed-list design proposal

An editable table with a string in one cell and an integer in the next needs a statically known element type that can hold either value. Homogeneous lists currently return an element with one known LLVM representation. Using a string list for mixed data would make reads, stores, aliases, and calls disagree about the stored type.

## Type and representation

- Start with a closed union of `str` and `int` as the element type. Keep that type fixed throughout the list's lifetime. A table contains references to lists with the same element type. Rows can have different lengths.
- Store each cell inline with a discriminant and a payload. The string case stores a pointer and UTF-8 byte length. The integer case stores the signed 32-bit value. Lay out the payload with the native ABI and verify its size, alignment, and overflow limits in the runtime and code generator.
- Do not alias a homogeneous list as a mixed list. Mutating the mixed alias would violate the homogeneous alias's element type. Any conversion to mixed storage would construct a new list.
- Indexing returns the union. Use an explicit checked extraction or a control-flow type test before treating it as a string or an integer. Replacement evaluates the new value and writes its tag and payload. Operators and overload resolution need explicit rules for union operands. Printing dispatches on the tag. Ordering and removal need their own semantics before implementation.

## Cost and validation

Cells have an additional tag and alignment overhead. Indexing remains constant time after the bounds check, with an additional tag check for extraction. Shared lists still need stable object identity. Each string cell can reference immutable UTF-8 bytes under the current process-lifetime model. A later reclamation model must release the old payload correctly when a cell changes cases.

The compiler does not yet implement union types, narrowing by tag, or a mixed-element ABI. The mixed-table example is rejected today. Before accepting syntax, prototype and test cells with embedded NUL, replacement across tags through aliases, argument and return flows, mixed row lengths, bounds checks, and explicit extraction failures.
