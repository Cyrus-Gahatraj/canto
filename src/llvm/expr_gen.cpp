// expr_gen.cpp — LLVM IR generation for expressions
//
// An "expression" is anything that produces a value: literals (42, 3.14, "hi"),
// variables, arithmetic (+, -, *, /), comparisons (<, ==, etc.), logical ops,
// array literals, and array indexing.
//
// The main entry point is `expr_gen(node)`. It looks at what kind of AST node
// it received and emits the matching LLVM instruction(s).
//
// LLVM quick reference:
//   Value*       — the result of any instruction (a "virtual register")
//   ConstantInt  — a compile-time constant integer
//   ConstantFP   — a compile-time constant floating-point number
//   AllocaInst*  — a stack slot (local variable storage)
//   IRBuilder    — writes instructions at the current position in the function

#include "context.hpp"
#include "helpers.hpp"
#include "canto/symtable.h"

using namespace llvm;

// ---------------------------------------------------------------------------
// Identifier — load a variable's current value
// ---------------------------------------------------------------------------

static Value* gen_ident(Node *node) {
    // In REPL mode, variables live in a global array instead of stack slots
    if (IsRepl && TheReplGlobals)
        return repl_load(node->ident.sym);

    std::string key = sym_name(node->ident.sym);

    auto it = NamedValues.find(key);
    if (it == NamedValues.end()) {
        fprintf(stderr, "Compiler Error: Undefined variable '%s'\n", key.c_str());
        return nullptr;
    }

    // Load the value from the alloca slot (stack → register)
    AllocaInst *slot = it->second;
    return Builder->CreateLoad(slot->getAllocatedType(), slot, "load." + key);
}

// ---------------------------------------------------------------------------
// Literal values — compile-time constants
// ---------------------------------------------------------------------------

static Value* gen_int_lit(Node *node) {
    // i64 = 64-bit signed integer
    return ConstantInt::get(Type::getInt64Ty(*TheContext), node->int_lit.value);
}

static Value* gen_double_lit(Node *node) {
    // f64 = 64-bit IEEE 754 floating-point
    return ConstantFP::get(Type::getDoubleTy(*TheContext), node->double_lit.value);
}

static Value* gen_bool_lit(Node *node) {
    // i1 = 1-bit integer (LLVM's boolean type)
    return ConstantInt::get(Type::getInt1Ty(*TheContext), node->bool_lit.value ? 1 : 0);
}

static Value* gen_string_lit(Node *node) {
    if (!TheSymtable) {
        fprintf(stderr, "Compiler Error: No symbol table set\n");
        return nullptr;
    }
    const Symbol *sym = &TheSymtable->syms[node->string_lit.sym];
    // Create a global char array with the string and return a pointer to it
    return Builder->CreateGlobalString(
        StringRef(sym->start, sym->length), "str"
    );
}

// ---------------------------------------------------------------------------
// Unary operations — negate, logical NOT
// ---------------------------------------------------------------------------

static Value* gen_unary(Node *node) {
    Value *operand = expr_gen(node->unary.expr);
    if (!operand) return nullptr;

    switch (node->unary.op) {
        case TK_MINUS:
            // Negate: use float negate for doubles, integer negate for ints
            if (operand->getType()->isDoubleTy())
                return Builder->CreateFNeg(operand, "fneg");
            return Builder->CreateNeg(operand, "neg");

        case TK_BANG:
            // Logical NOT: flips every bit (works for i1 booleans)
            return Builder->CreateNot(operand, "not");

        default:
            fprintf(stderr, "Compiler Error: Unknown unary operator\n");
            return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Binary operations — arithmetic, comparison, logical
// ---------------------------------------------------------------------------

static Value* gen_binary(Node *node) {
    Value *L = expr_gen(node->binary.left);
    Value *R = expr_gen(node->binary.right);
    if (!L || !R) return nullptr;

    // If either operand is a double, promote the other to double too
    bool is_float = L->getType()->isDoubleTy() || R->getType()->isDoubleTy();
    if (is_float) {
        L = coerce_value(L, Type::getDoubleTy(*TheContext));
        R = coerce_value(R, Type::getDoubleTy(*TheContext));
    }

    switch (node->binary.op) {

        // Arithmetic
        case TK_PLUS:
            return is_float ? Builder->CreateFAdd(L, R, "fadd")
                            : Builder->CreateAdd (L, R, "add");
        case TK_MINUS:
            return is_float ? Builder->CreateFSub(L, R, "fsub")
                            : Builder->CreateSub (L, R, "sub");
        case TK_STAR:
            return is_float ? Builder->CreateFMul(L, R, "fmul")
                            : Builder->CreateMul (L, R, "mul");
        case TK_SLASH:
            return is_float ? Builder->CreateFDiv(L, R, "fdiv")
                            : Builder->CreateSDiv(L, R, "sdiv");
        case TK_PERCENTAGE:
            return is_float ? Builder->CreateFRem(L, R, "frem")
                            : Builder->CreateSRem(L, R, "srem");

        // Comparison — always returns i1 (boolean)
        case TK_LT:
            return is_float ? Builder->CreateFCmpOLT(L, R, "flt")
                            : Builder->CreateICmpSLT(L, R, "ilt");
        case TK_GT:
            return is_float ? Builder->CreateFCmpOGT(L, R, "fgt")
                            : Builder->CreateICmpSGT(L, R, "igt");
        case TK_LEQ:
            return is_float ? Builder->CreateFCmpOLE(L, R, "fle")
                            : Builder->CreateICmpSLE(L, R, "ile");
        case TK_GEQ:
            return is_float ? Builder->CreateFCmpOGE(L, R, "fge")
                            : Builder->CreateICmpSGE(L, R, "ige");
        case TK_EQUAL:
            return is_float ? Builder->CreateFCmpOEQ(L, R, "feq")
                            : Builder->CreateICmpEQ (L, R, "ieq");
        case TK_NOT_EQUAL:
            return is_float ? Builder->CreateFCmpONE(L, R, "fne")
                            : Builder->CreateICmpNE (L, R, "ine");

        // Logical
        case TK_KW_AND:
            return Builder->CreateAnd(L, R, "and");
        case TK_KW_OR:
            return Builder->CreateOr (L, R, "or");

        default:
            fprintf(stderr, "Compiler Error: Unknown binary operator\n");
            return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Array literal — [1, 2, 3]
// ---------------------------------------------------------------------------

static Value* gen_array(Node *node) {
    if (node->array.count == 0) {
        fprintf(stderr, "Compiler Error: Empty array literals are not supported\n");
        return nullptr;
    }

    // Evaluate the first element to determine the element type
    Value *first = expr_gen(node->array.exprs[0]);
    if (!first) return nullptr;
    Type *elem_ty = first->getType();

    // Allocate a stack array of the right size in the entry block
    Value *count_val = ConstantInt::get(Builder->getInt64Ty(), node->array.count);
    Function *fn = Builder->GetInsertBlock()->getParent();
    IRBuilder<> entry_builder(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    AllocaInst *arr = entry_builder.CreateAlloca(elem_ty, count_val, "arr");

    // Store each element into the array
    for (uint32_t i = 0; i < node->array.count; i++) {
        Value *elem = (i == 0) ? first : expr_gen(node->array.exprs[i]);
        if (!elem) return nullptr;

        // Coerce if element type differs from the array's element type
        if (elem->getType() != elem_ty) {
            Value *coerced = coerce_value(elem, elem_ty);
            if (coerced) elem = coerced;
        }

        // arr[i] = elem
        Value *idx = ConstantInt::get(Builder->getInt64Ty(), i);
        Value *ptr = Builder->CreateGEP(elem_ty, arr, idx, "arr.elem");
        Builder->CreateStore(elem, ptr);
    }

    return arr;
}

// ---------------------------------------------------------------------------
// Array index — arr[i]
// ---------------------------------------------------------------------------

static Value* gen_index(Node *node) {
    // Evaluate the array (produces a pointer to the first element)
    Value *base = expr_gen(node->index.left);
    if (!base) return nullptr;

    // Evaluate the index
    Value *idx = expr_gen(node->index.index);
    if (!idx) return nullptr;

    // Find out what type each element is — we need this for GEP
    Type *elem_ty = nullptr;
    if (node->index.left->kind == NODE_IDENT) {
        std::string vname = sym_name(node->index.left->ident.sym);
        auto it = VariableElementTypes.find(vname);
        if (it != VariableElementTypes.end())
            elem_ty = it->second;
    }

    // Fall back to i64 if we have no type information
    if (!elem_ty) {
        if (base->getType()->isPointerTy()) {
            elem_ty = Type::getInt64Ty(*TheContext);
        } else {
            fprintf(stderr, "Compiler Error: Index target is not a pointer\n");
            return nullptr;
        }
    }

    // Compute the pointer to arr[i] and load from it
    Value *ptr = Builder->CreateGEP(elem_ty, base, idx, "idx.ptr");
    return Builder->CreateLoad(elem_ty, ptr, "idx.val");
}

// ---------------------------------------------------------------------------
// expr_gen — main dispatch function
// ---------------------------------------------------------------------------

// Generates LLVM IR for any expression node.
// Returns the LLVM Value* that holds the result, or nullptr on error.
Value* expr_gen(Node *node) {
    if (!node) return nullptr;

    switch (node->kind) {
        case NODE_IDENT:      return gen_ident(node);
        case NODE_INT_LIT:    return gen_int_lit(node);
        case NODE_DOUBLE_LIT: return gen_double_lit(node);
        case NODE_BOOL_LIT:   return gen_bool_lit(node);
        case NODE_STRING_LIT: return gen_string_lit(node);
        case NODE_UNARY:      return gen_unary(node);
        case NODE_BINARY:     return gen_binary(node);
        case NODE_ARRAY:      return gen_array(node);
        case NODE_INDEX:      return gen_index(node);

        // Parenthesised expression: (expr) — just unwrap it
        case NODE_GROUP:
            return expr_gen(node->group.expr);

        // Bare dot: '.' inside a when predicate arm — refers to the subject value
        // Example: when x { . > 4: { write "big" } }
        //   Here '.' resolves to the current value of x
        case NODE_DOT:
            if (node->dot.left == nullptr && node->dot.field_sym == 0) {
                if (!WhenSubject) {
                    fprintf(stderr, "Compiler Error: '.' used outside of a when predicate arm\n");
                    return nullptr;
                }
                return WhenSubject;
            }
            fprintf(stderr, "Compiler Error: Unhandled dot expression in when predicate\n");
            return nullptr;

        // A bare keyword used as an expression evaluates to 0
        case NODE_KEYWORD:
            return ConstantInt::get(Builder->getInt32Ty(), 0);

        // Calls and edits can produce values — delegate to stmt_gen
        case NODE_CALL:
        case NODE_EDIT:
            return stmt_gen(node);

        default:
            fprintf(stderr, "Compiler Error: Unhandled expression node kind %d\n", node->kind);
            return nullptr;
    }
}
