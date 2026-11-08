# Approved long integer policies

The owner explicitly selected R2, P1, and O1: checked long-to-int narrowing, explicit long/real conversions, and ordinary function overloads. Stage 6 implements these choices. Unselected alternatives below record the reviewed tradeoffs and are not supported modes.

## Proposed common rules

- Make long a signed, two's-complement 64-bit type. Keep char unsigned 8-bit, short signed 16-bit, and int signed 32-bit. int converts implicitly to long; long converts to int only with an explicit int(value).
- Infer an integer literal as int if its signed value fits int; otherwise infer long if it fits long. Reject values outside signed 64-bit range. Parse unary-minus literals so -2147483648 remains int and -9223372036854775808 is valid long. No suffix or unsigned integer syntax is added.
- Distinguish a literal from arithmetic: 2147483648 is long, while 2147483647 + 1 uses int arithmetic and wraps to -2147483648. Same-type small integer arithmetic keeps its existing width. Mixed integer arithmetic uses the wider lossless integer type, so int plus long is long.
- Preserve existing fixed-width arithmetic wrap rules for addition, subtraction, multiplication, negation, increment, and left shift, including long. Integer division truncates toward zero; division by zero is a runtime error. LONG_MIN divided by -1 wraps to LONG_MIN. Right shifts preserve the sign for signed values. Reject negative shift counts and counts at least the left operand's width, before any count narrowing.
- Keep explicit real-to-integer conversion's nearest-even rounding and low-bit behavior, extended correctly to 64-bit long. Reject NaN and infinity. Runtime printing, comparisons, parameters/returns, nullable values, struct/class fields, division, and shifts must all use the full 64-bit value.
- Integer slices, fixed integer arrays, collection indices, and the special for-loop counter remain int. Do not introduce a long collection type or silently narrow a long index.

These rules necessarily change the old tests that expect long to wrap at 32 bits. Update those specific expected results and replace the obsolete 32-bit literal-limit failure with a signed 64-bit limit failure. Keep independent int wrap tests, add long boundary tests, and retain every other existing integration scenario. This is a specification change, not a weakening of test assertions.

## R: Explicit long to int conversion

- R1, recommended: preserve low 32 bits, interpreted as signed int, consistent with existing explicit integer conversions. For example, int(2147483648) is -2147483648. Arithmetic overflow still wraps at the operand type's width.
- R2: reject an out-of-range explicit long-to-int conversion at runtime. Arithmetic still wraps, but this new narrowing operation uses a checked policy instead of the current low-bit convention. Existing char/short conversion rules would stay unchanged unless separately approved.

## P: Mixed long and real values

- P1, recommended: preserve the language's lossless implicit-conversion promise. A general 64-bit long cannot convert implicitly to float or double because neither represents every long exactly. Mixed long/real arithmetic and assignments require an explicit real conversion, such as double(value). Existing int/float mixed arithmetic can still use double, and float can widen to double.
- P2: permit implicit long-to-double conversion and use double for mixed long/real arithmetic. This is convenient but can lose integer precision beyond 2^53; the specification would need an explicit exception to its lossless-conversion rule. long-to-float assignment would still require an explicit cast.

Explicit long-to-real conversions are allowed under either policy and follow floating-point rounding. Neither policy silently changes the int/long literal classification based on the expected assignment type.

## O: Function overload scope and ranking

- O1, recommended for the requested overload behavior: add ordinary function overload groups within a single declaring file. Exact parameter types win; otherwise prefer lossless integer widening, then real widening, then lossless integer-to-real conversion. Within a widening category prefer the smaller sufficient destination width. Reject a call if no candidate is viable or if no candidate is at least as good for every argument and strictly better for at least one argument. Do not add conversion costs across arguments to manufacture a winner. No implicit narrowing, return-type-only overloads, constructor overloads, method overloads, or overrides are added.
- O2: keep duplicate ordinary function names as compile errors in this stage, document that no overload selection is supported, and schedule overload groups separately. This keeps stage 6 focused on integer width but leaves the requested overload feature deferred.

For O1, literal types are determined before overload ranking: f(1) selects an int overload over long, while f(2147483648) selects long. Duplicate signatures remain errors. Existing visibility, local-name precedence, and conflicts between public declarations in different source files remain unchanged; overload groups must not silently merge across modules. main remains a unique entry point. The exact ranking applies only to conversions permitted by the selected P policy.

## Planned tests after approval

Cover INT_MAX+1 as a literal versus expression, INT_MIN, LONG_MAX, LONG_MIN, values outside long, positive/negative printing, -O0/-O2/-O3 wrap behavior, long division and shifts including huge long shift counts, explicit narrowing, implicit widening and forbidden narrowing, long/real precision policy, nullable/field/argument/return flows, and overload selection/ambiguity or duplicate rejection according to O. Preserve prior compiler, privacy, relocation, static import, package integrity, TextMate, and VS Code host checks.

The compiler and extension versions still describe unreleased working source. Final release version/tag selection remains a separate owner review; do not overwrite previous release archives or tags to distribute the changed integer behavior.
