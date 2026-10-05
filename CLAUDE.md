# CLAUDE.md

Canto is a small language compiler. A Rust CLI drives a C front end (lexer and parser), which hands the AST to a C++ back end that generates LLVM IR. Programs either become native binaries (IR → `clang -O2`) or run line by line in a JIT REPL.

## Commands

```sh
nix develop                      # toolchain: rustup, LLVM 18, clang 18, gperf, bear
cargo build
cargo test                       # integration tests, see "Tests" below
cargo run -- run file.ct         # compile and run
cargo run -- build file.ct       # binary written to ./build/<stem>
cargo run                        # REPL
```

The linker prints `ld: warning: directory not found ... libiconv` under Nix on macOS. The warning is harmless.

## Pipeline

```
main.rs (clap) → ffi::Engine → compile() [src/c/compiler.c]
  → lexer.c        tokens + interned symbols (SymTable)
  → parser.c       Pratt parser → arena-allocated AST (Node)
  → eval_stmts:     `get "f"` → load_module: lex/parse f.ct, recurse (compiler.c)
  → codegen (C++)  stmt_gen / expr_gen / var_gen / fn_gen → one LLVM Module with `main`
  → file mode:     codegen_dump → build/.tmp/<stem>.ll → clang (in main.rs) → build/<stem>
  → REPL mode:     jit_run (ORC LLJIT), then codegen_init() for the next line
```

- **FFI:** `src/ffi/ffi.rs` declares `canto_ctx_create`, `canto_ctx_free` and `compile`. `Engine` owns the `CantoContext*`.
- **Symbols:** each symbol ID is an index into `SymTable.syms`. **ID 0 means "none"**, and real IDs start at 1. In C++, `sym_name(id)` turns an ID back into its text.
- **Codegen state is global.** `src/llvm/context.hpp` holds `TheContext`, `TheModule`, `Builder`, `NamedValues`, `LoopStack`, `KeywordModifiers`, `VariableElementTypes`, `IsRepl` and `WhenSubject`. `codegen_init()` resets all of them.
- **Codegen return values:** gen functions return `nullptr` on error. `codegen_eval_expr` turns that into `-1`. A statement that produces no value returns a dummy `i32 0` constant rather than `nullptr`.
- **Modules:** `get "path"` without a `{ }` block loads a Canto module, and with a block it declares C functions (`get "m" {}` links a library alone). The parser sets `get.is_canto` from that. Modules are resolved in `compiler.c`, not codegen. The path is relative to the importing file, and `.ct` is appended if missing. The module's statements are compiled into the same `main` at the import point. Every module shares the caller's `SymTable`, and `loaded_modules` (absolute paths, including the main file) makes each file load once. `gen_get` rejects a module `get` nested inside a block.
- **REPL persistence:** variables live in a fixed `int64_t ReplStorage[65536]` indexed by symbol ID (`src/llvm/repl.cpp`). Doubles, bools and pointers are bitcast into those slots. Generated code calls `repl_set_type` to record each slot's type, and slot 0 (`REPL_RESULT_SLOT`) holds the last expression's value for printing.

## Where things live

| Change | Files |
|---|---|
| New token or keyword | `include/private/token_kinds.def`. The gperf keyword table is **generated from it** by `build.rs`; there is no `.gperf` file to edit. Add keywords between `TK_KW(BEGINNING)` and `TK_KW(ENDING)`. |
| New AST node | `include/private/ast_kinds.def` plus a union arm in `include/canto/ast.h` |
| Statement syntax | `parse_stmt` in `src/c/parser.c` |
| Expression syntax / precedence | the `global_rules[]` Pratt table and `Precedence` in `include/canto/parser.h` |
| Statement codegen | `stmt_gen` dispatch in `src/llvm/stmt_gen.cpp` (`let`/`edit` → `var_gen.cpp`, functions → `fn_gen.cpp`) |
| Expression codegen | `expr_gen` in `src/llvm/expr_gen.cpp` |
| Keyword modifier attributes (`write.edit { .end: "" }`) | `src/c/keywords/<kw>.c` (registered in `keyword_list.c`) or `local_registry` in `src/c/keyword_modifier.c` |
| Editor highlighting | `tools/tree-sitter-canto/grammar.js` and `queries/highlights.scm`. The generated `src/parser.c` is committed, so regenerate it with `tree-sitter generate` after syntax changes. |

**`build.rs` lists every C and C++ source file by hand.** When you add a `.c` file under `src/c/` or a `.cpp` file under `src/llvm/`, add it there too. Only `src/c/keywords/*.c` is picked up automatically.

## Parser behavior that isn't obvious

- **Newlines end expressions.** The infix loop in `parse_expr` skips spaces and comments but stops at `TK_NEWLINE`. A `;` terminator is optional.
- **Function vs. value `let`:** after the name, if `(...)` is followed by `{`, the `let` is a function; otherwise it's a value (`let x (5)`). See `parse_let_declaration`.
- **`Ident expr` as a statement** (`Writef "hi"`) is a call to a keyword modifier. The default case of `parse_stmt` detects it and routes it through `parse_keyword_stmt`.
- **`or` has two meanings.** After an `if` block it means else-if; inside a condition it's boolean OR.
- **`if cond | loop { }`** is a while loop, stored as `NODE_IF` with `is_loop = true`.
- **`loop expr { }`** always treats `expr` as a repeat count and never as a condition. `loop { }` loops forever.
- **In `when`, `.` means the subject.** A predicate arm evaluates with `WhenSubject` set to the value being matched. Arms never fall through, and the `;` after an arm has no effect.
- **String interpolation happens in the lexer.** It splits `"a `x` b"` into several tokens, and only an identifier or a number is allowed between the backticks.

## Tests

Each test is a `tests/sources/<name>.ct` file plus a `#[test]` in `tests/check.rs`:

```rust
#[test]
fn foo_test() {
    CantoTest::new("foo.ct").assert_output("expected stdout");
}
```

The harness runs `canto build` and executes the result from `build/<stem>`. **The assertion in `check.rs` is the source of truth.** The "Expected Terminal Output" comments inside the `.ct` files have drifted in `when.ct` and `loops.ct`.

## Known limitations (as of 2026-09-29)

- Functions are integer-only. `gen_fn` makes every parameter and return value `i64`, and it parses type annotations but ignores them.
- `design`, `try`, `ask`, `optional` and `error` are reserved keywords with no implementation. `..` (parent access) is parsed but not generated.
- `ArenaBlock.used` and `ArenaBlock.capacity` are `uint8_t` while blocks are 64 KiB (`include/canto/arena.h`), so almost every allocation starts a new block.
- Nothing ever calls `free_source_map`. `init_lexer` sets up a SymTable that `compile()` immediately overwrites.
- `loop t { }` with a bool `t` runs zero times without any error.
- The REPL prints string variables as `""`.
- `get` can't declare variadic C functions (`printf`), and in the REPL its declarations last only for that line.
- Modules have no namespaces or exports. Every top-level name is shared, and a module's source, tokens and AST are never freed.
- The REPL forgets functions after each line, including functions from an imported module.
- `main.rs` ignores a failed `compile()`, so `canto run` still calls clang and prints a "no such file" error after the real one.

## Conventions

- C uses `snake_case` and tabs. The C++ globals use LLVM-tutorial `PascalCase` (`TheModule`, `Builder`).
- C++ functions called from C are declared `extern "C"` and listed in `include/canto/*.h`, inside `#ifdef __cplusplus` guards.
- Growable arrays use the `EXTEND_ARENA_CAPACITY` / `EXTEND_ARENA` macros from `memory.h`. `reallocate` exits the process on out-of-memory.
- Diagnostics: call `append_diag(diags, msg, span, DIAG_PHASE_*, DIAG_ERROR)`. The message must be a string literal or interned, because it isn't copied.
- Git commit messages are short, lowercase and imperative ("add when keyword support").
