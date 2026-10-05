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

// A definition only records the function. gen_call compiles it once per set
// of argument types: add(1, 2) becomes "add(i64,i64)" and add(1.5, "x")
// becomes "add(double,ptr)". A parameter annotated `x: double` always takes
// that type. ponytail: a function that is never called is never checked.
std::map<std::string, Node*> FnTemplates;

Value* gen_fn(Node *node) {
    FnTemplates[sym_name(node->fn.name_sym)] = node;
    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// Return types found by a probe compile: probe function → type of its first
// `return` (void for a bare one). nullptr until a return is seen.
static std::map<Function*, Type*> ProbeReturns;

// Turns an annotation like `double` into its type. nullptr + error if unknown.
static Type* annotation_type(Node *ann) {
    std::string n = sym_name(ann->ident.sym);
    Type *t = keyword_name_to_llvm_type(n.c_str());
    if (!t) fprintf(stderr, "Compiler Error: Unknown type '%s' (use int, double, bool or string)\n", n.c_str());
    return t;
}

// Compiles `def` as a function named `fname`. The function's own scope
// (parameters, locals, loops) is set up fresh and the caller's is restored.
static Function* build_fn(Node *def, const std::string &fname,
                          const std::vector<Type*> &params, Type *ret, bool probe = false) {
    FunctionType *fn_type = FunctionType::get(ret, params, false);
    Function *fn = Function::Create(fn_type, Function::ExternalLinkage, fname, TheModule.get());
    if (probe) ProbeReturns[fn] = nullptr;

    auto saved_vals  = NamedValues;
    auto saved_elems = VariableElementTypes;
    auto saved_loops = LoopStack;
    Value *saved_subject  = WhenSubject;
    BasicBlock *saved_bb  = Builder->GetInsertBlock();
    LoopStack.clear();
    WhenSubject = nullptr;

    Builder->SetInsertPoint(BasicBlock::Create(*TheContext, "entry", fn));
    for (auto &arg : fn->args()) {
        std::string pname = sym_name(def->fn.params[arg.getArgNo()]->param.name_sym);
        arg.setName(pname);
        AllocaInst *slot = alloca_at_entry(fn, arg.getType(), pname);
        Builder->CreateStore(&arg, slot);
        NamedValues[pname] = slot;
        if (auto *arr = dyn_cast<ArrayType>(arg.getType()))
            VariableElementTypes[pname] = arr->getElementType();
    }

    bool ok = stmt_gen(def->fn.body) != nullptr;

    // falling off the end returns the zero value (0, 0.0, false, null)
    if (!Builder->GetInsertBlock()->getTerminator()) {
        if (ret->isVoidTy()) Builder->CreateRetVoid();
        else                 Builder->CreateRet(Constant::getNullValue(ret));
    }

    NamedValues          = saved_vals;
    VariableElementTypes = saved_elems;
    LoopStack            = saved_loops;
    WhenSubject          = saved_subject;
    if (saved_bb) Builder->SetInsertPoint(saved_bb);

    return ok ? fn : nullptr;
}

// Compiles one specialization of a Canto function. With no return
// annotation, the body is first compiled as a throwaway probe (returning
// i64) to find the type of its first `return`, then compiled for real.
static Function* specialize(Node *def, const std::string &fname, const std::vector<Type*> &params) {
    if (def->fn.return_type) {
        Type *ret = annotation_type(def->fn.return_type);
        return ret ? build_fn(def, fname, params, ret) : nullptr;
    }

    Function *before = TheModule->empty() ? nullptr : &TheModule->getFunctionList().back();
    if (!build_fn(def, fname, params, Builder->getInt64Ty(), true)) return nullptr;
    Function *probe = TheModule->getFunction(fname);
    Type *ret = ProbeReturns[probe] ? ProbeReturns[probe] : Builder->getInt64Ty();
    ProbeReturns.erase(probe);
    probe->deleteBody();

    if (probe->use_empty()) {
        probe->eraseFromParent();
    } else {
        // Mutual recursion: functions compiled during the probe call it with
        // its guessed i64 return. Drop them all; they get rebuilt below.
        std::vector<Function*> made;
        for (auto it = before ? std::next(before->getIterator()) : TheModule->begin();
             it != TheModule->end(); ++it)
            made.push_back(&*it);
        for (Function *f : made) f->dropAllReferences();
        for (Function *f : made) f->eraseFromParent();
    }
    return build_fn(def, fname, params, ret);
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

// Compiles:  get c:m { sqrt(x: double): double }
Value* gen_get(Node *node) {
    // compile() loads top-level modules; anything reaching here is nested
    if (node->get.is_canto) {
        fprintf(stderr, "Compiler Error: 'get module' is only allowed at the top level\n");
        return nullptr;
    }
    std::string lib = node->get.lib_sym ? sym_name(node->get.lib_sym).substr(2) : "";  // drop "c:"
    if (!lib.empty()) {
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

    auto tmpl = FnTemplates.find(name);
    if (tmpl != FnTemplates.end()) {
        Node *def = tmpl->second;
        if (def->fn.param_count != node->call.arg_count) {
            fprintf(stderr, "Compiler Error: '%s' expects %u arguments, got %u\n",
                    name.c_str(), def->fn.param_count, node->call.arg_count);
            return nullptr;
        }

        std::vector<Value*> args;
        std::vector<Type*>  types;
        std::string fname = name + "(";
        for (uint32_t i = 0; i < node->call.arg_count; i++) {
            Value *v = expr_gen(node->call.args[i]);
            if (!v) return nullptr;
            if (Node *ann = def->fn.params[i]->param.type_ann) {
                Type *want = annotation_type(ann);
                if (!want) return nullptr;
                if (!(v = coerce_value(v, want))) {
                    fprintf(stderr, "Compiler Error: Argument %u of '%s' has the wrong type\n", i + 1, name.c_str());
                    return nullptr;
                }
            }
            args.push_back(v);
            types.push_back(v->getType());
            std::string ts;
            raw_string_ostream os(ts);
            v->getType()->print(os);
            fname += (i ? "," : "") + os.str();
        }
        fname += ")";

        Function *fn = TheModule->getFunction(fname);
        if (!fn && !(fn = specialize(def, fname, types))) return nullptr;

        if (fn->getReturnType()->isVoidTy()) {
            Builder->CreateCall(fn, args);
            return ConstantInt::get(Builder->getInt32Ty(), 0);
        }
        return Builder->CreateCall(fn, args, "call." + name);
    }

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
    Function *cur    = Builder->GetInsertBlock()->getParent();
    Type     *ret_ty = cur->getReturnType();
    bool      probe  = ProbeReturns.count(cur) > 0;
    Value    *val    = nullptr;

    if (node->return_.value && !(val = expr_gen(node->return_.value))) return nullptr;

    // A probe only records the first return's type; its body is thrown away
    if (probe) {
        if (!ProbeReturns[cur]) ProbeReturns[cur] = val ? val->getType() : Builder->getVoidTy();
        Builder->CreateRet(Constant::getNullValue(ret_ty));
    } else if (!val) {
        if (!ret_ty->isVoidTy()) {
            fprintf(stderr, "Compiler Error: Bare 'return' in a function that returns a value\n");
            return nullptr;
        }
        Builder->CreateRetVoid();
    } else {
        Value *coerced = coerce_value(val, ret_ty);
        if (!coerced) {
            fprintf(stderr, "Compiler Error: A function's returns must all have the same type\n");
            return nullptr;
        }
        Builder->CreateRet(coerced);
    }

    // Create an unreachable "dead" block for any code that follows the return.
    // This is a standard LLVM pattern — the dead block is usually eliminated by
    // the optimizer.
    Function   *fn   = Builder->GetInsertBlock()->getParent();
    BasicBlock *dead = BasicBlock::Create(*TheContext, "after.return", fn);
    Builder->SetInsertPoint(dead);

    return ConstantInt::get(Builder->getInt32Ty(), 0);
}
