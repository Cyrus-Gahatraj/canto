# Canto

A small, poetic programming language that compiles to native code through LLVM, and whose keywords you can reshape from inside your own program.

```
~ Shall I compare thee to a summer's day?
let lovely 10
let season "summer"

if lovely > 5 {
    write "Thou art more lovely than a `season` day"
} else {
    write "Rough winds do shake the darling buds of May"
}
```

```
$ canto run sonnet.ct
Thou art more lovely than a summer day
```

> **Status:** early and experimental (v0.1.0). The syntax is still changing, and several reserved keywords (`design`, `try`, `ask`, `optional`, `error`) are not implemented yet.

## A tour of the language

### Bindings

`let` binds a name to a value. There is no `=` in a binding, which leaves `=` free to mean equality everywhere.

```
let permissions 644
let pi 3.14
let ready true
let greeting "hello"
let total (5 * 4 + 3 / 9)
```

### Changing a value: `.edit` and the current value `.`

Values change through an `edit` block. Inside the block, a bare `.` stands for the current value, like the `.` directory in a shell.

```
permissions.edit { 755 }       ~ replace the value
permissions.edit { . - 55 }    ~ compute from the current value → 700
```

### Strings and interpolation

Put a variable or number between backticks inside a string to interpolate it:

```
let dir "usr"
write "Accessing: /`dir`/script.sh"
```

### Comments

```
~ a line comment

~~
  a block comment
~~
```

### Conditions: `if` / `or` / `else`

`or <condition>` after a block works like `else if`. Inside a condition, `or` and `and` are the boolean operators.

```
if lovely > 5 and temperate > 5 {
    write "more lovely"
} or short_summer {
    write "too short a date"
} else {
    write "eternal summer"
}
```

Comparison operators are `=  !=  <  >  <=  >=`. Arithmetic is `+ - * / %`, and `!` and unary `-` are the prefix operators.

### Loops

```
loop 3 {                   ~ repeat a fixed number of times
    write "again"
}

loop {                     ~ loop forever until `break`
    if pulse = 2 { break }
    pulse.edit { . + 1 }
}

if machine_on | loop {     ~ while machine_on is true
    write "What did you dream?"
    break
}
```

`break` and `continue` work in every loop form.

### Pattern matching: `when`

An arm matches either a value (numbers or strings) or a predicate, where `.` is the value being matched. An arm that starts with a comparison like `> 90` can leave out the `.`. `_` is the default arm. The first arm that matches runs.

```
when score {
    > 90:   { write "A" }
    > 75:   { write "B" }
    . > 60 and . < 70: { write "C" }
    _:      { write "F" }
}

when greeting {
    "hi":    { write "casual" }
    "hello": { write "formal" }
    _:       { write "unknown" }
}
```

### Functions

A `let` whose name is followed by a parameter list and a body defines a function:

```
let square(x) {
    return x * x
}

let hypot2(a, b) {
    return square(a) + square(b)
}

write hypot2(3, 4)    ~ 25
```

A function takes any type of value. Each call compiles a copy for the types it passes, and the return type comes from the first `return`:

```
let add(a, b) {
    return a + b
}

write add(2, 3)         ~ 5
write add(1.5, 2.25)    ~ 3.750000

let pick(flag, a, b) {
    if flag { return a }
    return b
}

write pick(true, "yes", "no")   ~ yes
```

You can annotate parameters and the return type to fix them. Arguments are then converted to that type:

```
let scale(x: double, k): double {
    return x * k
}

write scale(3, 2)       ~ 6.000000
```

All the `return`s in one function must have the same type.

### Arrays and custom types

```
let MString type.edit { .name: "string", .is_many: true }

let words :MString ["Hello", "Array"]
write words[0]
```

### Reshaping keywords

Canto's main idea is that its built-in keywords are configurable. Edit a keyword and bind the result to a new name, and you have a variant of that keyword:

```
let Writef write.edit { .end: "" }   ~ a `write` that doesn't add a newline

Writef "hello "
write "world"                        ~ → hello world
```

By convention, a keyword variant has a PascalCase name so it stands out from ordinary variables.

### Modules and C libraries: `get`

`get` with a path pulls in another Canto file. The path needs no quotes (add them if it has spaces) and is relative to the importing file, and `.ct` is optional. If the path is a folder, every `.ct` file in it loads, in name order. The file's top-level code runs at the point of the `get`, and its functions and variables are visible after it. A file only loads once, so repeated or circular imports are safe.

```
get lib/mathx          ~ loads lib/mathx.ct

write square(7)
```

A `c:` prefix means a C library instead. The block declares the functions to use from it. Leave out the name to use the C standard library. The C types are `int`, `long`, `char`, `float`, `double`, `string`, `ptr` and `void`.

```
get c:m {              ~ links libm
    sqrt(x: double): double
}

get {                  ~ libc
    puts(s: string): int
}

write sqrt(16.0)
```

Modules share one namespace, so a name defined in two files clashes.

## Using the CLI

```sh
canto init my-project         # start a project (or `canto init` for the current folder)
canto update                  # rebuild canto from the latest source
canto run path/to/file.ct     # compile to a native binary and run it
canto build path/to/file.ct   # compile to ./build/<name>
canto                         # start the interactive REPL (type `exit` to quit)
```

`canto init` creates a project like this:

```
main.ct          get editor
                 write "hello world"
editor/
  write.ct       write variants, like Writef (no newline)
  types.ct       array types: Strings, Ints, Doubles, Bools
```

`get editor` loads the whole `editor` folder. Edit those files to reshape the keywords your project uses, or add new ones.

Source files use the `.ct` extension. `build` and `run` generate LLVM IR, link it with `clang -O2`, and write the executable to `./build/`. The REPL instead compiles each line in memory with LLVM's ORC JIT, and your variables persist from one line to the next.

## Install

**Linux and macOS:**

```sh
curl -fsSL https://raw.githubusercontent.com/Cyrus-Gahatraj/canto/main/install.sh | sh
```

It installs LLVM 18, clang and gperf (apt, dnf or Homebrew) and Rust if missing, then builds `canto` into `~/.cargo/bin`.

**Windows** (PowerShell):

```powershell
irm https://raw.githubusercontent.com/Cyrus-Gahatraj/canto/main/install.ps1 | iex
```

Canto runs inside WSL on Windows. The script sets up WSL if needed, installs Canto there, and adds a `canto` command to Windows that forwards to it. Use `/` in file paths.

Set `CANTO_BRANCH` to install a branch other than `main`.

## Building from source

You need:

- **Rust**: edition 2024, so 1.85 or newer
- **LLVM 18**: `llvm-config` must be on your `PATH`
- **clang**: links the generated IR into executables
- **gperf**: generates the keyword lookup table at build time

The easiest way to get all of these is the Nix flake:

```sh
nix develop          # or `direnv allow`, which uses the bundled .envrc
cargo build --release
cargo test
```

The compiler binary ends up at `target/release/canto`.

## Project layout

```
src/main.rs            CLI (clap): run / build / REPL
src/ffi/               Rust ↔ C bridge (Engine)
src/c/                 Front end in C: lexer, Pratt parser, symbol table, diagnostics
src/c/keywords/        Keyword modifier definitions (write, type)
src/llvm/              Back end in C++: LLVM IR generation, JIT, REPL storage
include/canto/         Public C headers
include/private/*.def  X-macro lists of token kinds and AST node kinds
tests/                 Integration tests: sources/*.ct, compiled and run by check.rs
tools/tree-sitter-canto/  Tree-sitter grammar and syntax highlighting
```

## License

MIT © 2026 Cyrus Gahatraj
