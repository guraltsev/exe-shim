# C Programming Style

These rules control C programming style. Use `docs/documentation_rules.md` for
public API and file documentation.

Write code as an explained calculation: the reader should understand the role
of each small block before reading every line inside it.

## Reading order

Organize files by narrative importance.

1. Put system and standard-library includes first, grouped by origin.
2. Put project headers after system headers. Include the header a `.c` file
   implements before other project headers when practical.
3. Put the public entrypoint, command handler, or central workflow before
   low-level implementation details.
4. Put file-local helpers later, grouped by purpose. Declare a helper before
   its first use with a `static` prototype when its definition follows.
5. Use short section comments when a file has distinct groups of helpers.

Do not start with a long catalog of constants, helpers, data types, and
configuration unless the reader needs those names immediately.

File comments and public headers describe public purpose and observable
behavior. They should not explain where to start reading the source file; use
comments for local source narration.

## Narrated code blocks

A nontrivial function should read as a sequence of short, narrated blocks. Put
a one- or two-line comment before each block that explains what the next few
lines accomplish. Do not narrate syntax the types and names already make
clear.

Use block comments before:

- validation or normalization steps,
- loops that collect, transform, filter, copy, or verify data,
- corner-case handling,
- filesystem, subprocess, network, or other side-effecting operations,
- conversions between API, UTF-8, and UTF-16 representations,
- construction of summaries, manifests, reports, or output records,
- fallback behavior,
- safety checks,
- resource acquisition, transfer of ownership, and cleanup,
- Windows API calls whose error handling or lifetime is not obvious,
- dense imperative sequences.

A good comment describes the move being made in the function's argument. It
answers questions such as:

- What are we establishing?
- What case are we handling?
- What object are we constructing?
- What invariant or safety property are we preserving?
- What side effect is about to happen?

A reader should rarely encounter more than about ten lines of nontrivial
imperative code without a leading comment. Tiny one-line operations do not need
comments.

Bad:

```c
/* Copy each path. */
for (size_t i = 0; i < path_count; ++i)
  copy_path(paths[i]);
```

Good:

```c
/* Resolve every input before creating outputs, so invalid paths fail without
 * leaving a partially populated destination directory. */
for (size_t i = 0; i < path_count; ++i)
  resolve_path(paths[i], resolved_paths[i]);
```

Bad:

```c
/* Check whether step_dir is non-NULL. */
if (step_dir != NULL)
  write_summary(step_dir);
```

Good:

```c
/* Emit diagnostics only when the caller requested an output directory. The
 * summary reflects the files actually produced by this run. */
if (step_dir != NULL)
  write_summary(step_dir);
```

A comment is bad if it merely repeats the next line. A comment is good if it
lets the reader skip the next few lines.

## Control flow, errors, and helpers

Keep simple logic local and readable in one pass. Prefer direct iteration and
immediate error signaling over helper indirection.

Do:

- keep validation and action together when used once,
- iterate directly over values,
- return an explicit status or report an error immediately when a value is
  invalid,
- use straightforward control flow over extra abstraction,
- split code into helpers when the helper names a real concept, isolates a
  side effect, removes genuine repetition, or makes behavior independently
  testable.

For Win32 calls, preserve `GetLastError()` before making another API call when
the error will be reported or translated. State ownership in the name, API
documentation, or nearby comment when it is not obvious. Each acquired `HANDLE`
or allocation must have one clearly reachable release path; a `cleanup:` label
is appropriate when it makes that path simpler and safer.

Do not:

- extract one-off validation into tiny `static` helpers,
- add normalization or conversion layers unless behavior requires them,
- introduce extra state or containers that are not needed for correctness,
- hide the main behavior behind indirection,
- create tiny one-use helpers merely to make the code look organized.

Heuristic: if the reader must jump between functions to follow one simple
operation, inline it.

## Data and state

Use a `struct` when values have meaningful related state, invariants, or a
natural ownership boundary. Do not create a `struct` merely to namespace
loosely related functions.

Keep state close to the behavior that owns it. Avoid generic registries,
configuration objects, or framework-style architecture unless the behavior
requires them.

Make ownership and mutability visible in interfaces: use `const` for inputs
that are not modified, document who releases returned allocations or handles,
and avoid exposing mutable global state. Prefer fixed-width integer types when
the width matters; use `size_t` for object sizes and indexes.

## Constants

Constants are useful when they are reused, conceptually important, or likely to
change. Otherwise, keep values close to where they are used.

Keep one-use constants local to their consumer. Put file-scope constants at
the top only when they are genuinely file-relevant. Every global constant
must have a short comment explaining its role.

Bad:

```c
static const wchar_t* const user_fields[] = { L"id", L"name", L"email" };
static const wchar_t* const order_fields[] = { L"id", L"total", L"created_at" };
```

Good:

```c
void export_rows(const struct user* user, const struct order* order)
{
  /* Keep each export schema next to the row it constructs. */
  const wchar_t* const user_fields[] = { L"id", L"name", L"email" };
  export_user_row(user, user_fields, _countof(user_fields));

  /* The order row is independent and has its own schema. */
  const wchar_t* const order_fields[] = { L"id", L"total", L"created_at" };
  export_order_row(order, order_fields, _countof(order_fields));
}
```

## Explanatory prose

When writing examples, reviews, or design notes, introduce the code before
showing it. Do not put the only explanation after the code as a retrospective
tour.

Prefer direct, readable code over generic, reusable, or enterprise-style
architecture.
