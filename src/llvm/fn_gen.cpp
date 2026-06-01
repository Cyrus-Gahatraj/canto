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

using namespace llvm;

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
        args.push_back(v);
    }

    // Emit the call instruction; result is the function's return value
    return Builder->CreateCall(fn, args, "call." + name);
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
