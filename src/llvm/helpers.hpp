// helpers.hpp — Shared utility functions for the Canto LLVM code generator
//
// These helpers wrap common LLVM patterns so the rest of the codegen files
// read like straightforward C++ rather than LLVM API calls.
//
// Include this wherever you need:
//   - Stack variable allocation
//   - Numeric type coercion (int ↔ double)
//   - Boolean normalization for branch conditions
//   - printf declaration
//   - Keyword attribute resolution

#pragma once

#include "context.hpp"

using namespace llvm;

// ---------------------------------------------------------------------------
// Stack allocation helpers
// ---------------------------------------------------------------------------

// Allocates a local variable slot at the very top of the function's entry
// block. LLVM's mem2reg pass requires allocas to be in the entry block to
// promote them to registers (better performance).
//
// Example: alloca_at_entry(fn, i64_type, "x") → an alloca for a 64-bit int named "x"
AllocaInst* alloca_at_entry(Function *fn, Type *type, const std::string &name);

// ---------------------------------------------------------------------------
// Type coercion helpers
// ---------------------------------------------------------------------------

// Coerces `val` to `target_type` if the types differ.
// Handles: int→double, double→int, int→int (with sign extension/truncation).
// Returns the original value unchanged if types already match.
// Returns nullptr if coercion is not possible.
Value* coerce_value(Value *val, Type *target_type);

// Ensures `val` is a 1-bit integer (i1) suitable for use in a branch.
// If it's already i1, returns it as-is.
// Otherwise generates: val != 0  (which produces an i1).
Value* ensure_bool(Value *val, const std::string &name = "cond");

// ---------------------------------------------------------------------------
// printf declaration helper
// ---------------------------------------------------------------------------

// Returns (or lazily declares) the C `printf` function in the current module.
// Safe to call multiple times — only declares once.
//
// LLVM IR signature: i32 printf(i8*, ...)
Function* get_or_declare_printf();

// ---------------------------------------------------------------------------
// Keyword edit helpers
// ---------------------------------------------------------------------------

// Extracts the attribute name from an edit-pair node.
// Returns empty string if the pair has no named field.
//
// An edit pair is something like:  { end: "\n" }
// where "end" is the attribute name and "\n" is the value.
std::string resolve_edit_attr_name(Node *pair, KeywordMeta *meta);

// Extracts the string value from an edit-pair node's value expression.
// Handles: string literals, identifier names, and bool literals (→ "true"/"false").
// Returns empty string if the value cannot be resolved as a string.
std::string resolve_edit_attr_value(Node *pair);

// ---------------------------------------------------------------------------
// LLVM type helpers
// ---------------------------------------------------------------------------

// Maps a keyword type-name string to an LLVM Type*.
// Understands: "int"/"integer" → i64, "double"/"float" → f64,
//              "bool"/"boolean" → i1, "string" → i8*
// Returns nullptr if the name is not recognized.
Type* keyword_name_to_llvm_type(const char *name);
