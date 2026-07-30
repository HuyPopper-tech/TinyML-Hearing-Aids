# Project-owned Doxygen normalization design

Date: 2026-07-30

## Goal

Normalize Doxygen documentation across all project-owned public headers without
changing APIs or runtime behavior. The six header edits already present in the
working tree define the desired multiline style and must be preserved.

## Scope

Include public headers under `app/`, `board/`, `common/`, `drivers/`, and
`services/`.

Exclude:

- `libs/`;
- raw and generated model assets under `models/`;
- build directories and tool-generated files;
- source-file implementation comments that are not public API documentation.

## Documentation contract

Every public API Doxygen comment uses a multiline block:

```c
/**
 * @brief Short purpose statement.
 * @param argument Ownership, valid range, or buffer expectations.
 * @return Meaning of each relevant result.
 */
```

The documentation must:

- place `@brief`, each `@param`, and `@return` on separate lines;
- describe ownership and lifetime when pointers or retained callbacks are used;
- identify foreground, ISR, reentrancy, blocking, or allocation constraints
  when they affect safe use;
- document output initialization and NULL handling where the API defines them;
- avoid comments that merely repeat an obvious function name;
- retain the existing project terminology and architecture boundaries.

Void functions use `@return None.` to match the user-established pattern.
Callback typedefs and non-trivial public types receive multiline Doxygen when
their invocation context or field semantics are not otherwise self-evident.

## Enforcement

Extend the style contract tests before production edits. The regression test
will scan in-scope public headers and fail when it finds a single-line Doxygen
block. It will also compare documented `@param` names with public function
parameters and require a `@return` entry for public functions, while allowing
documented exceptions for macros and simple declarations.

The test is intentionally limited to formatting and declaration/documentation
consistency. It will not attempt subjective natural-language scoring.

## Validation

Use a red-green workflow:

1. Add the style regression and observe failure on current headers.
2. Normalize and enrich in-scope public Doxygen blocks.
3. Run the focused style test.
4. Run full Python test discovery.
5. Run Debug and Release incremental builds.
6. Run `git diff --check`, inspect the final diff, commit, and push `main`.

Generated artifacts and firmware behavior must remain byte-for-byte unchanged.
