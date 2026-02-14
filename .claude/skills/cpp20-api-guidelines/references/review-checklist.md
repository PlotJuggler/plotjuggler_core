# Review Checklist

Run this checklist before returning recommendations or code.

## API shape
1. Is the API minimal and coherent (no convenience overload sprawl)?
2. Are ownership and lifetime explicit in every public boundary?
3. Is mutation explicit and const applied by default?
4. Could a free function replace a class without losing invariants?
5. Are side effects isolated from pure logic?
6. Is error handling consistent for this layer (absl::StatusOr / exceptions / optional)?
7. Are out-parameters used only when allocation/copy costs justify them?
8. Is duplicated business logic removed without premature abstraction?

## Misuse resistance
9. Are boolean flags in public APIs replaced by `enum class` or strong option types?
10. Are single-argument constructors `explicit` unless intentional implicit conversion is needed?
11. Are adjacent same-type parameters prevented with strong domain types?
12. Are sentinel values replaced with `std::optional`?
13. Are fallible return values marked `[[nodiscard]]`?
14. Are objects fully valid after construction (no two-phase init)?

## Abseil usage
15. Are hash containers using `absl::flat_hash_map`/`set` instead of `std::unordered_*`?
16. Are ordered containers using `absl::btree_map`/`set` instead of `std::map`/`set`?
17. Are string operations using `absl::StrCat`/`StrJoin`/`StrSplit`/`StrFormat` where appropriate?
18. Are time values using `absl::Duration` / `absl::Time` instead of raw integers?
19. Is `absl::StatusOr<T>` used for recoverable failures (not exceptions for control flow)?
20. Is `.ok()` checked before accessing `StatusOr` values?
21. Are custom types hashable via `AbslHashValue` if used as hash keys?

## Testability and safety
22. Can core logic be tested deterministically without global state?
23. Are thread-safety guarantees documented where concurrency exists?
24. Are `absl::Mutex` annotations (`ABSL_GUARDED_BY`, etc.) applied where applicable?
25. Is API naming consistent, clear, and explicit about side effects/ownership?
26. Does the parameter passing follow the Abseil Tip #234 guide (value/ref/view)?
