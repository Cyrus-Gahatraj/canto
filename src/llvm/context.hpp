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

using namespace llvm;

// shared across all codegen modules
extern std::unique_ptr<LLVMContext> TheContext;
extern std::unique_ptr<IRBuilder<>> Builder;
extern std::unique_ptr<Module>      TheModule;
extern std::map<std::string, AllocaInst*> NamedValues;
extern SymTable *TheSymtable;

// loop-block stack: (continue target, break target)
extern std::vector<std::pair<BasicBlock*, BasicBlock*>> LoopStack;

// keyword modifier instances: sym -> instance (for `keyword.edit { }`)
extern std::map<uint32_t, KeywordInstance*> KeywordModifiers;

// Tracks variable element types for correct GEP on index load/store
extern std::map<std::string, Type*> VariableElementTypes;

// REPL persistent global array (external reference into runtime module)
extern GlobalVariable *TheReplGlobals;
extern bool IsRepl;

// forward declarations
Value* expr_gen(Node *node);
Value* stmt_gen(Node *node);

std::string sym_name(uint32_t sym_id);

Value *repl_load(uint32_t sym_id);
Value *repl_store(uint32_t sym_id, Value *val);

