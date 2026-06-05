// context.cpp — Definitions for all shared compiler state
//
// This file owns the actual storage for all the global objects declared in
// context.hpp. Every other file just uses them via `extern`.
//
// `sym_name()` is a tiny utility that converts a numeric symbol ID into the
// original source text (e.g. symbol #42 → "myVariable").

#include "context.hpp"

using namespace llvm;

// ---------------------------------------------------------------------------
// Core LLVM objects (one per compilation unit)
// ---------------------------------------------------------------------------

std::unique_ptr<LLVMContext> TheContext;
std::unique_ptr<IRBuilder<>> Builder;
std::unique_ptr<Module>      TheModule;

// ---------------------------------------------------------------------------
// Variable tracking
// ---------------------------------------------------------------------------

// Maps variable name → its alloca slot in the current function
std::map<std::string, AllocaInst*> NamedValues;

// The Canto symbol table — set by the Rust side before codegen starts
SymTable *TheSymtable = nullptr;

// ---------------------------------------------------------------------------
// Loop tracking
// ---------------------------------------------------------------------------

// Stack of (continue-target, break-target) blocks for nested loops
std::vector<std::pair<BasicBlock*, BasicBlock*>> LoopStack;

// ---------------------------------------------------------------------------
// Keyword modifier instances
// ---------------------------------------------------------------------------

// Maps symbol ID → keyword instance (for `write.edit { end: "" }` style syntax)
std::map<uint32_t, KeywordInstance*> KeywordModifiers;

// ---------------------------------------------------------------------------
// Array element type tracking
// ---------------------------------------------------------------------------

// Needed so we know what type to use when computing an array index (GEP)
std::map<std::string, Type*> VariableElementTypes;

// ---------------------------------------------------------------------------
// REPL state
// ---------------------------------------------------------------------------

bool            IsRepl         = false;
GlobalVariable *TheReplGlobals = nullptr;

// ---------------------------------------------------------------------------
// 'when' predicate subject
// ---------------------------------------------------------------------------

// Points to the value of the when-subject while evaluating predicate arms.
// A bare '.' node resolves to this during predicate pattern evaluation.
Value *WhenSubject = nullptr;

// ---------------------------------------------------------------------------
// sym_name — convert a symbol ID to its source text
// ---------------------------------------------------------------------------

// Given a symbol ID (a number the parser uses internally), returns the
// original variable/function name as a C++ string.
// Returns "<unknown_N>" if the table is missing or the ID is out of range.
std::string sym_name(uint32_t sym_id) {
    if (!TheSymtable || sym_id == 0 || sym_id > TheSymtable->count)
        return "<unknown_" + std::to_string(sym_id) + ">";
    const Symbol *s = &TheSymtable->syms[sym_id];
    return std::string(s->start, s->length);
}
