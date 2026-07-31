# C++ Coding Style

Write modern C++23 that makes invalid memory access and ownership mistakes
unrepresentable where practical. Use `docs/documentation_rules.md` for public
API and file documentation guidance.

## Safety

- Never use raw owning pointers, `malloc`/`free`, `new`/`delete`, fixed-size
  buffers, or manual allocation-size arithmetic.
- Prefer `std::string`, `std::wstring`, `std::vector`, `std::filesystem::path`,
  and range or stream operations. Let these types own storage and track size.
- Use `std::optional` or `std::expected` for fallible values; make ownership
  explicit with RAII types such as `std::unique_ptr` or a small handle wrapper.
- When a Windows API requires a mutable NUL-terminated buffer, create it from
  an owning `std::vector` and append the terminator explicitly.
- Avoid hand-written parsers when Windows or the standard library has an
  established implementation. Use `CommandLineToArgvW` for Windows command
  lines.

## Dependencies

Manage third-party dependencies exclusively through Conan. Prefer mature,
well-maintained libraries over custom implementations when they add real value;
record each direct dependency in `conanfile.py` and link its CMake target.

## Reading order

Organize files by narrative importance.

1. Put system and standard-library includes first, grouped by origin.
2. Put project headers after them. A `.cpp` file should include the header it
   implements before other project headers when practical.
3. Put the public entrypoint, command handler, or central workflow before
   low-level implementation details.
4. Put file-local helpers later, grouped by purpose. Use a declaration before
   first use when a definition naturally follows the main workflow.
5. Use short section comments when a file has distinct groups of helpers.

Do not begin with a long catalog of constants, helpers, types, and
configuration unless the reader needs those names immediately. File comments
and public headers describe public purpose and observable behavior; use local
comments to narrate source-level decisions.

## Narrated workflows

Write code as an explained calculation: a nontrivial function should read as a
sequence of short, narrated blocks. Put a one- or two-line comment before a
block when it explains the purpose, invariant, or consequence of the next few
lines. Do not narrate syntax that clear names and types already express.

Use a leading comment for validation or normalization; nontrivial loops;
corner cases; filesystem, subprocess, or other side effects; UTF-8/UTF-16 or
API conversions; fallback behavior; safety checks; resource acquisition or
release; and Windows API calls whose lifetime or error handling is non-obvious.
As a rule of thumb, do not leave more than about ten lines of nontrivial
imperative code without context. Tiny one-line operations need no comment.

A useful comment lets the reader skip the block: it says what is being
established, which case is handled, which object is being constructed, which
invariant is preserved, or which side effect is intended. Do not restate the
next line.

```cpp
// Resolve every input before creating outputs, so invalid paths fail without
// leaving a partially populated destination directory.
for (const auto& path : paths) {
  resolved_paths.push_back(resolve_path(path));
}
```

## Control flow, errors, and helpers

Keep simple logic local and readable in one pass. Prefer direct iteration,
clear early returns, and immediate error propagation over helper indirection.

- Keep one-use validation and its action together.
- Split code into a helper only when it names a real concept, owns a resource
  boundary or side effect, removes genuine repetition, or is independently
  testable.
- Keep the main behavior visible; if following a simple operation requires
  jumping between functions, prefer inlining it.
- Prefer straightforward control flow over framework-style abstraction,
  unnecessary normalization layers, or containers and state that correctness
  does not require.

For Win32 calls, capture `GetLastError()` before another API call can overwrite
it when the error will be reported or translated. Express ownership with RAII;
when that is impossible, document the releasing operation next to acquisition.
Every acquired resource must have one clear, reachable release path.

## Data, state, and constants

Use a `struct` or `class` when values have related state, invariants, or a
natural ownership boundary. Do not introduce a type solely to namespace loosely
related functions. Keep state close to the behavior that owns it, and avoid
generic registries, configuration objects, mutable globals, or framework-style
architecture unless the behavior needs them.

Make mutability visible in interfaces: accept `std::span`, `std::string_view`,
or `const&` where they accurately express non-owning input, and return an owned
value when the caller should own the result. Use fixed-width integer types when
the width matters, and `std::size_t` for object sizes and indexes.

Constants are useful when reused, conceptually important, or likely to change;
otherwise keep values beside their consumer. Keep one-use constants local. Put
namespace-scope constants at the top only when genuinely file-relevant, and
give each non-obvious one a short comment explaining its role.

## Readability

Keep the main workflow direct. Use named helpers only for real concepts,
resource boundaries, or independently testable behavior. Add comments before
non-obvious Windows API lifetimes, fallback paths, and safety invariants.

When writing examples, reviews, or design notes, introduce code before showing
it rather than explaining it only afterward. Prefer direct, readable code over
generic, reusable, or enterprise-style architecture.
