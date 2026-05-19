#pragma once
#include "canto/ast.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <map>
#include <string>
#include <memory>

using namespace llvm;

// shared across all codegen modules
extern std::unique_ptr<LLVMContext> TheContext;
extern std::unique_ptr<IRBuilder<>> Builder;
extern std::unique_ptr<Module>      TheModule;
extern std::map<std::string, AllocaInst*> NamedValues;

// forward declarations
Value* expr_gen(Node *node);
Value* stmt_gen(Node *node);

