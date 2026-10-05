// fn_gen.cpp — LLVM IR generation for functions: define, call, return
//
// This file handles three statement kinds related to functions:
//
//   NODE_FN     — function definition:   fn add(a, b) { give a + b }
//   NODE_CALL   — function call:         add(1, 2)
//   NODE_RETURN — return statement:      give value
//
// In LLVM, a function is represented by a `Function` object that contains
// basic blocks. Each basic block is a sequence of instructions ending in
// a terminator (ret, br, etc.).
//
// This file is separate from stmt_gen.cpp to keep each file focused and small.

#include "context.hpp"
#include "helpers.hpp"

#include <llvm/Support/DynamicLibrary.h>

using namespace llvm;

// Libraries named by `get`, as clang flags ("-lm -lfoo"). Read by main.rs.
std::string LinkLibs;

extern "C" const char* codegen_link_libs(void) {
    return LinkLibs.c_str();
}

// ---------------------------------------------------------------------------
// NODE_FN — function definition
// ---------------------------------------------------------------------------

// Compiles a function definition like:  fn add(a, b) { give a + b }
//
// Steps:
//   1. Build the LLVM function type (return type + parameter types)
//   2. Create the LLVM Function object
//   3. Name each parameter
//   4. Create the entry basic block and switch to it
//   5. Allocate stack slots for each parameter (so they can be mutated)
//   6. Generate the function body
//   7. Add an implicit `return 0` if the body has no explicit return
//   8. Restore the builder's insert point to wherever we were before
Value* gen_fn(Node *node) {
    std::string name = sym_name(node->fn.name_sym);

    // All parameters default to i64 (64-bit integer).
    // Full type inference would be a separate pass.
    std::vector<Type*> param_types(node->fn.param_count, Type::getInt64Ty(*TheContext));

    // Default return type: i64
    Type *ret_type = Type::getInt64Ty(*TheContext);

    // Create the LLVM function type:  i64 (i64, i64, ...)
    FunctionType *fn_type = FunctionType::get(ret_type, param_types, /*isVarArg=*/false);

    // Create the function in the module with external linkage (callable from outside)
    Function *fn = Function::Create(fn_type, Function::ExternalLinkage, name, TheModule.get());

    // Give each LLVM argument the name it has in the source
    uint32_t idx = 0;
    for (auto &arg : fn->args()) {
        arg.setName(sym_name(node->fn.params[idx]->param.name_sym));
        idx++;
    }

    // Save the current insert point so we can restore it after the function
    BasicBlock *saved_bb = Builder->GetInsertBlock();

    // Create the function's entry basic block and start inserting there
    BasicBlock *entry = BasicBlock::Create(*TheContext, "entry", fn);
    Builder->SetInsertPoint(entry);

    // Allocate stack slots for each parameter.
    // This makes parameters mutable (they can be reassigned inside the function).
    idx = 0;
    for (auto &arg : fn->args()) {
        std::string pname = std::string(arg.getName());
        AllocaInst *slot = alloca_at_entry(fn, arg.getType(), pname);
        Builder->CreateStore(&arg, slot);
        NamedValues[pname] = slot;
        idx++;
    }

    // Generate IR for the function body
    stmt_gen(node->fn.body);

    // If the last block has no terminator (no explicit return), add `return 0`
    if (!Builder->GetInsertBlock()->getTerminator())
        Builder->CreateRet(ConstantInt::get(ret_type, 0));

    // Restore the builder to where it was before we started this function
    if (saved_bb)
        Builder->SetInsertPoint(saved_bb);

    // The function definition itself evaluates to 0 (it's a statement)
    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// ---------------------------------------------------------------------------
// NODE_GET — declare C functions and link their library
// ---------------------------------------------------------------------------

// C type names use C sizes (int is 32 bits), unlike Canto's own `int`.
static Type* c_type(Node *ann) {
    if (!ann) return Type::getVoidTy(*TheContext);
    std::string n = sym_name(ann->ident.sym);
    if (n == "int")                    return Type::getInt32Ty(*TheContext);
    if (n == "long")                   return Type::getInt64Ty(*TheContext);
    if (n == "char")                   return Type::getInt8Ty(*TheContext);
    if (n == "float")                  return Type::getFloatTy(*TheContext);
    if (n == "double")                 return Type::getDoubleTy(*TheContext);
    if (n == "string" || n == "ptr")   return PointerType::get(*TheContext, 0);
    if (n == "void")                   return Type::getVoidTy(*TheContext);
    fprintf(stderr, "Compiler Error: Unknown C type '%s' "
                    "(use int, long, char, float, double, string, ptr or void)\n", n.c_str());
    return nullptr;
}

// Compiles:  get "m" { sqrt(x: double): double }
Value* gen_get(Node *node) {
    if (node->get.lib_sym) {
        std::string lib = sym_name(node->get.lib_sym);
        bool is_path = lib.find('/') != std::string::npos;

        if (IsRepl) {
            // ponytail: dlopen failure is ignored; the JIT reports the missing symbol on call
            std::string file = is_path ? lib : "lib" + lib +
#ifdef __APPLE__
                ".dylib";
#else
                ".so";
#endif
            sys::DynamicLibrary::LoadLibraryPermanently(file.c_str());
        } else {
            std::string flag = is_path ? lib : "-l" + lib;
            if ((" " + LinkLibs + " ").find(" " + flag + " ") == std::string::npos)
                LinkLibs += (LinkLibs.empty() ? "" : " ") + flag;
        }
    }

    for (uint32_t i = 0; i < node->get.decl_count; i++) {
        Node *decl = node->get.decls[i];
        std::string name = sym_name(decl->fn.name_sym);

        std::vector<Type*> params;
        for (uint32_t p = 0; p < decl->fn.param_count; p++) {
            Type *t = c_type(decl->fn.params[p]->param.type_ann);
            if (!t) return nullptr;
            if (t->isVoidTy()) {
                fprintf(stderr, "Compiler Error: Parameter %u of '%s' needs a type\n", p + 1, name.c_str());
                return nullptr;
            }
            params.push_back(t);
        }
        Type *ret = c_type(decl->fn.return_type);
        if (!ret) return nullptr;

        FunctionType *ty = FunctionType::get(ret, params, /*isVarArg=*/false);
        Function *existing = TheModule->getFunction(name);
        if (existing && existing->getFunctionType() != ty) {
            fprintf(stderr, "Compiler Error: '%s' is already declared with a different signature\n", name.c_str());
            return nullptr;
        }
        if (!existing)
            Function::Create(ty, Function::ExternalLinkage, name, TheModule.get());
    }

    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// ---------------------------------------------------------------------------
// NODE_CALL — function call
// ---------------------------------------------------------------------------

// Compiles a function call like:  add(1, 2)
//
// Looks up the function by name in the module, checks argument count,
// evaluates each argument, and emits a call instruction.
Value* gen_call(Node *node) {
    // Call target must be an identifier (direct calls only, no function pointers yet)
    if (node->call.callee->kind != NODE_IDENT) {
        fprintf(stderr, "Compiler Error: Call target must be an identifier\n");
        return nullptr;
    }

    std::string name = sym_name(node->call.callee->ident.sym);
    Function   *fn   = TheModule->getFunction(name);

    if (!fn) {
        fprintf(stderr, "Compiler Error: Undefined function '%s'\n", name.c_str());
        return nullptr;
    }

    // Check that the caller passes the right number of arguments
    if (fn->arg_size() != node->call.arg_count) {
        fprintf(stderr, "Compiler Error: '%s' expects %zu arguments, got %u\n",
                name.c_str(), fn->arg_size(), node->call.arg_count);
        return nullptr;
    }

    // Evaluate each argument expression
    std::vector<Value*> args;
    for (uint32_t i = 0; i < node->call.arg_count; i++) {
        Value *v = expr_gen(node->call.args[i]);
        if (!v) return nullptr;
        Type *want = fn->getArg(i)->getType();
        Value *c = coerce_value(v, want);
        if (!c) {
            fprintf(stderr, "Compiler Error: Argument %u of '%s' has the wrong type\n", i + 1, name.c_str());
            return nullptr;
        }
        args.push_back(c);
    }

    // Emit the call instruction; result is the function's return value
    Type *ret = fn->getReturnType();
    if (ret->isVoidTy()) {
        Builder->CreateCall(fn, args);
        return ConstantInt::get(Builder->getInt32Ty(), 0);
    }
    Value *result = Builder->CreateCall(fn, args, "call." + name);

    // Widen C results (int, char, float) to the types Canto computes with
    if (ret->isIntegerTy() && !ret->isIntegerTy(64))
        return coerce_value(result, Builder->getInt64Ty());
    if (ret->isFloatTy())
        return coerce_value(result, Builder->getDoubleTy());
    return result;
}

// ---------------------------------------------------------------------------
// NODE_RETURN — return statement
// ---------------------------------------------------------------------------

// Compiles a return statement like:  give value   (or bare `give`)
//
// After emitting the return, we create a "dead" basic block to absorb any
// code that follows the return in the source (unreachable code). This keeps
// LLVM happy — every basic block needs a valid terminator.
Value* gen_return(Node *node) {
    if (node->return_.value) {
        Value *val = expr_gen(node->return_.value);
        if (!val) return nullptr;

        // Cast the return value to the function's declared return type if needed
        Function *fn     = Builder->GetInsertBlock()->getParent();
        Type     *ret_ty = fn->getReturnType();
        Value    *coerced = coerce_value(val, ret_ty);
        if (coerced) val = coerced;

        Builder->CreateRet(val);
    } else {
        // Bare `give` with no value → void return
        Builder->CreateRetVoid();
    }

    // Create an unreachable "dead" block for any code that follows the return.
    // This is a standard LLVM pattern — the dead block is usually eliminated by
    // the optimizer.
    Function   *fn   = Builder->GetInsertBlock()->getParent();
    BasicBlock *dead = BasicBlock::Create(*TheContext, "after.return", fn);
    Builder->SetInsertPoint(dead);

    return ConstantInt::get(Builder->getInt32Ty(), 0);
}
