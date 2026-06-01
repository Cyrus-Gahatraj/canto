// context.hpp — Shared compiler state for the Canto LLVM code generator
//
// This file is the "global blackboard" — every code generation file includes
// it so they can all share the same LLVM context, module, IR builder, and
// variable/loop tracking tables.
//
// If you're new to LLVM, think of it like this:
//   LLVMContext  — the LLVM "world" that owns all types and constants
//   Module       — the compiled program (holds functions, globals, etc.)
//   IRBuilder    — a cursor that writes LLVM instructions at the current position
//
// Everything here is declared `extern` so it lives in exactly one .cpp file
// (context.cpp) and is shared everywhere else.

#pragma once

#include "canto/ast.h"
#include "canto/symtable.h"
#include "canto/keyword_modifier.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/GlobalVariable.h>
#include <map>
#include <string>
#include <memory>
#include <vector>
#include <utility>

// ---------------------------------------------------------------------------
// Core LLVM objects — one per compilation unit
// ---------------------------------------------------------------------------

// The LLVM "world" — owns all types, constants, and metadata
extern std::unique_ptr<llvm::LLVMContext> TheContext;

// The IR builder — writes instructions at the current insert point
extern std::unique_ptr<llvm::IRBuilder<>> Builder;

// The LLVM module — the entire compiled program lives here
extern std::unique_ptr<llvm::Module> TheModule;

// ---------------------------------------------------------------------------
// Symbol table — maps variable names to their stack slots (AllocaInst)
// ---------------------------------------------------------------------------

// All currently visible local variables: name → alloca slot
extern std::map<std::string, llvm::AllocaInst*> NamedValues;

// The Canto symbol table — maps symbol IDs to their source text
extern SymTable *TheSymtable;

// ---------------------------------------------------------------------------
// Loop tracking — needed to wire up break/continue jumps
// ---------------------------------------------------------------------------

// Stack of (continue-target block, break-target block) for each active loop
extern std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>> LoopStack;

// ---------------------------------------------------------------------------
// Keyword modifier instances — runtime configuration objects
// ---------------------------------------------------------------------------

// Maps symbol ID → keyword configuration instance (e.g. `write.edit { end: "" }`)
extern std::map<uint32_t, KeywordInstance*> KeywordModifiers;

// ---------------------------------------------------------------------------
// Array element type tracking
// ---------------------------------------------------------------------------

// Tracks the element type of each array variable so GEP (pointer math) works correctly
extern std::map<std::string, llvm::Type*> VariableElementTypes;

// ---------------------------------------------------------------------------
// REPL (interactive mode) state
// ---------------------------------------------------------------------------

// True when running in REPL mode (interactive read-eval-print loop)
extern bool IsRepl;

// The global array that persists variable values between REPL evaluations
extern llvm::GlobalVariable *TheReplGlobals;

// ---------------------------------------------------------------------------
// Forward declarations — implemented in their respective .cpp files
// ---------------------------------------------------------------------------

// Generate LLVM IR for an expression node (returns a Value*)
llvm::Value* expr_gen(Node *node);

// Generate LLVM IR for a statement node (returns a Value*)
llvm::Value* stmt_gen(Node *node);

// Look up a symbol's name string from its ID in TheSymtable
std::string sym_name(uint32_t sym_id);

// Load/store a variable slot in the REPL global storage array
llvm::Value *repl_load(uint32_t sym_id);
llvm::Value *repl_store(uint32_t sym_id, llvm::Value *val);
