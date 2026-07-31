# C++ API Documentation Style

This file controls public C++ API and source-file documentation. Use
`docs/coding_rules.md` for code organization and inline comments.

API comments document the public contract, intended use, and important behavior
of headers, functions, macros, types, and callback interfaces. They should not
be source-code tours.

## Core rules

- Use Doxygen-compatible comments where generated API documentation is useful;
  ordinary `/* ... */` comments are sufficient otherwise.
- Start every public API comment with exactly one declarative summary sentence.
- Describe observable behavior, inputs, outputs, side effects, guarantees, and
  limitations.
- Mention important public entrypoints when that helps callers use the header
  or component.
- Do not describe the order of definitions in the file.
- Do not use API comments to compensate for missing block comments inside code.


## Header and source-file comments

Public header comments describe the component's responsibility and intended
use. Source-file comments are optional and should describe non-obvious
translation-unit behavior, not repeat the header. They may name entrypoints,
but only to explain how callers use the component.

Use this order:

1. Summary: one sentence describing the component's capability.
2. Extended summary: optional short paragraph describing important behavior,
   inputs and outputs, side effects, generated files, safety checks, or
   limitations.
3. See also: optional, only when another public header or function is essential
   context.

Do not include per-function parameter or return details in file comments.

Bad:

```c
/* Copy files into the build directory.
 *
 * Start with copy_inputs() for the library workflow. The support helpers below
 * it expand inputs and resolve output paths.
 */
```

Good:

```c
/* Copy declared pipeline inputs into a managed build directory.
 *
 * Call copy_inputs() to copy explicit paths into the selected build directory.
 * Unsafe output paths are rejected before files are written.
 */
```

Avoid source-navigation language, especially:

- "Start with ..."
- "read ... first"
- "helpers below"
- "below them"
- "remaining sections"

Usage-oriented entrypoint mentions are fine:

- "Call copy_inputs() to copy inputs into the build directory."
- "Call shim_main() from the command-line wrapper."
- "Most callers should use parse_manifest() rather than lower-level token
  helpers."

## Public functions

Use this order when the information is needed:

1. Summary, required.
2. Extended summary, optional.
3. Parameters, required when the function accepts public parameters.
4. Returns, required when the return value is meaningful.
5. Errors, required when failure is part of the interface.
6. Ownership, required when a parameter, allocation, or handle has a non-obvious
   lifetime.
7. Thread safety, optional.
8. Notes, optional.
9. Examples, useful for substantive public APIs.
10. See also, optional.

Function comments should describe the public contract. State accepted ranges,
encoding, buffer length units, nullability, ownership transfer, and failure
semantics when applicable. Put algorithm narration in code comments, not the
API comment.

## Public types, macros, and callbacks

Document opaque types by their role, lifetime, and creation/destruction
functions. For exposed `struct` types, document each field whose meaning,
ownership, or valid range is not self-evident. Document function-like macros
with the same care as functions, including whether arguments are evaluated
more than once. Document callback calling context, ownership, and whether the
callback may be retained.

## Section reference

### Summary

Exactly one declarative sentence describing the object.

Do not use filler, tutorial phrasing, or multiple sentences.

### Extended summary

Use only when the abstraction, behavior, scope, or interaction model is not
obvious from the summary.

Keep it to one to three short paragraphs. Cover the mental model, scope,
relationship to adjacent APIs, important guarantees, or limitations. Do not
include parameter lists or examples.

### Parameters

List every public parameter.

Format:

```text
Parameters
----------
`name` (`type`)
    Meaning, accepted values, nullability, and units of the parameter.
```

Describe semantics, not just the parameter name.

### Returns

Document the meaning of the return value, not just its type.

Format:

```text
Returns
-------
`type`
    Meaning of the returned value and how success or failure is represented.
```

For out-parameters, state which values are initialized on success and whether
they are modified on failure.

### Errors

Document intentional failure results and how callers obtain diagnostic details.
For Windows APIs, say whether the function preserves `GetLastError()`, sets it
on failure, or returns the underlying error code. Do not imply that `errno` is
meaningful unless the implementation explicitly sets it.

Format:

```text
Errors
------
`ERROR_INVALID_PARAMETER`
    Returned when `path` is NULL.
```

### Ownership

State who owns each allocation, string, and handle at API boundaries. Name the
matching release operation. Borrowed pointers must remain valid only for the
documented interval.

### Thread safety

State whether callers may invoke the interface concurrently and whether it
uses global state, static buffers, or thread-local storage.

### Notes

Use `Notes` for important semantics that do not belong elsewhere:

- invariants,
- guarantees,
- limitations,
- constraints,
- side effects,
- ordering, encoding, and initialization requirements,
- interoperability notes,
- reasons behavior should not be simplified.

Do not use `Notes` for filler, tutorials, or irrelevant implementation detail.

### Examples

Examples should be complete enough to compile with the public header, and must
show error handling when it is material.

Rules:

- Use named subsections such as `Basic usage`, `Composition`, `Variations`,
  `Edge cases`, `Interoperability`, or `Advanced usage` when needed.
- Initialize structures and release owned resources.
- Keep narrative short.

Base format:

```c
Examples
--------
Basic usage:

struct parser* parser = parser_create();
if (parser == NULL)
  return 1;

parse_record(parser, input);
parser_destroy(parser);
```

### See also

Include when at least one meaningful related target exists.

Rules:

- Use a bare target on the left-hand side.
- Put the target kind first in the description.
- Explain the relationship.
- Use two to six entries when the section is present.

Allowed target types:

- function
- type
- macro
- header
- external API
- document

Format:

```text
Target : function Description of the relationship.
    Optional continuation line.
```

For external URLs, use `document` and start the continuation line with the URL.
Do not include vague references or type metadata on the left-hand side.

## Private and internal comments

Private and internal objects include `static` functions and variables,
implementation-only types, and helpers outside the public API.

Private helpers are allowed only when they earn their existence. Prefer inline
code when it is clearer, keep one-off validation local, and avoid helper
indirection that forces the reader to jump around to understand simple
behavior.

If a private object exists, its comment may be brief, but it must state the
helper's internal role when the name and signature do not make it clear.

Use this lightweight order when sections are needed:

1. Summary, required.
2. Extended summary, optional for nontrivial helpers.
3. Parameters, optional when parameter meaning is not immediate.
4. Returns, optional when the return value has non-obvious meaning.
5. Errors, required for intentional internal failures.
6. Notes, optional for internal constraints, assumptions, side effects, or
   compatibility quirks.
7. Examples, rare; use only for private parsers, renderers, mini-protocols, or
   tricky edge cases.
8. See also, rare; use only when another helper, test, public API, or design
   document is essential context.

Minimal example:

```c
/* Return the first unquoted space that terminates a program path. */
static const wchar_t* find_program_end(const wchar_t* command_line);
```
