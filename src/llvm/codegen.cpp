// codegen.cpp — Public C API for the Canto LLVM code generator
//
// This file is the entry point that the Rust compiler driver calls.
// All functions are marked `extern "C"` so Rust (and C) can call them
// by name without C++ name mangling.
//
// Typical call sequence from the Rust side:
//   1. codegen_init()           — reset and prepare a fresh module
//   2. codegen_set_symtable()   — provide the symbol table
//   3. codegen_eval_expr() ...  — compile each top-level statement
//   4. codegen_finalize()       — add the final `return` instruction
//   5. jit_run()                — execute the compiled module
//   6. codegen_free()           — release all resources
//
// For dumping IR to a file or printing it:
//   codegen_dump(path)          — write LLVM IR text to a file
//   codegen_print_ir()          — print LLVM IR to stderr

#include "canto/codegen.h"
#include "canto/ast.h"
#include "canto/keyword_modifier.h"
#include "canto/repl.h"
#include "context.hpp"

using namespace llvm;

// The last expression result — used by the REPL to display the result
static Value *LastExprResult = nullptr;

// ---------------------------------------------------------------------------
// codegen_init — reset all state and create a fresh LLVM module
// ---------------------------------------------------------------------------

// Call this before compiling a new program or REPL line.
// Creates a new LLVMContext, Module, and IRBuilder, and generates an empty
// `main` function to emit statements into.
extern "C" void codegen_init(void) {
    // Clear all shared state from previous compilation
    NamedValues.clear();
    LoopStack.clear();
    KeywordModifiers.clear();
    VariableElementTypes.clear();
    LastExprResult  = nullptr;
    TheReplGlobals  = nullptr;

    // Create fresh LLVM objects
    TheContext = std::make_unique<LLVMContext>();
    TheModule  = std::make_unique<Module>("Canto", *TheContext);
    Builder    = std::make_unique<IRBuilder<>>(*TheContext);

    // Create the `main` function: int main()
    FunctionType *fn_type = FunctionType::get(Builder->getInt32Ty(), /*isVarArg=*/false);
    Function *main_fn = Function::Create(
        fn_type,
        Function::ExternalLinkage,
        "main",
        TheModule.get()
    );

    // Start inserting instructions into main's entry block
    BasicBlock *entry = BasicBlock::Create(*TheContext, "entry", main_fn);
    Builder->SetInsertPoint(entry);
}

// ---------------------------------------------------------------------------
// codegen_set_symtable — provide the symbol table to the code generator
// ---------------------------------------------------------------------------

// Must be called before any codegen calls. The symbol table maps numeric
// symbol IDs to their original source text (variable/function names).
extern "C" void codegen_set_symtable(SymTable *table) {
    TheSymtable = table;
}

// ---------------------------------------------------------------------------
// codegen_eval_expr — compile one AST node into the current module
// ---------------------------------------------------------------------------

// Returns 0 on success, -1 if code generation failed.
extern "C" int codegen_eval_expr(Node *node) {
    Value *result = stmt_gen(node);
    LastExprResult = result;
    if (!result) return -1;
    return 0;
}

// ---------------------------------------------------------------------------
// codegen_finalize — close the main function with a return statement
// ---------------------------------------------------------------------------

// Call this after all statements have been compiled. Adds `return <ret>` to
// the end of `main`. For programs, `ret` is typically 0 (success).
extern "C" void codegen_finalize(int ret) {
    Builder->CreateRet(Builder->getInt32(ret));
}

// ---------------------------------------------------------------------------
// codegen_finalize_repl — close the REPL's main function
// ---------------------------------------------------------------------------

// Like codegen_finalize, but also saves the last expression result into the
// REPL's persistent storage so it can be displayed after execution.
extern "C" void codegen_finalize_repl(void) {
    if (LastExprResult) {
        // Store slot 0 = the "last result" slot displayed by the REPL
        repl_store(REPL_RESULT_SLOT, LastExprResult);
    }
    Builder->CreateRet(Builder->getInt32(0));
}

// ---------------------------------------------------------------------------
// codegen_dump — write the LLVM IR to a file
// ---------------------------------------------------------------------------

// Writes the textual LLVM IR (the `.ll` format) to `output_path`.
// Useful for debugging — you can inspect what code was generated.
extern "C" void codegen_dump(const char *output_path) {
    std::error_code ec;
    raw_fd_ostream out(output_path, ec);

    if (ec) {
        fprintf(stderr, "Codegen Error: Could not open '%s': %s\n",
                output_path, ec.message().c_str());
        return;
    }

    TheModule->print(out, nullptr);
}

// ---------------------------------------------------------------------------
// codegen_print_ir — print the LLVM IR to stderr
// ---------------------------------------------------------------------------

// Prints the current module's IR to stderr. Handy for quick debugging.
extern "C" void codegen_print_ir(void) {
    TheModule->print(errs(), nullptr);
}

// ---------------------------------------------------------------------------
// codegen_free — release all code generation resources
// ---------------------------------------------------------------------------

// Call this after jit_run() (or codegen_dump()) to free all LLVM objects.
// After this call, codegen_init() must be called again before any new work.
extern "C" void codegen_free(void) {
    Builder.reset();
    TheModule.reset();
    TheContext.reset();
    NamedValues.clear();
    LoopStack.clear();
    LastExprResult = nullptr;
    TheReplGlobals = nullptr;

    // Free all keyword modifier instances
    for (auto &kv : KeywordModifiers)
        free_keyword_instance(kv.second);
    KeywordModifiers.clear();

    VariableElementTypes.clear();
    TheSymtable = nullptr;
}
