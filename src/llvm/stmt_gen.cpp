// stmt_gen.cpp — LLVM IR generation for statements (control flow + output)
//
// A "statement" is something that does work but doesn't necessarily produce
// a useful value: print to screen, branch on a condition, loop, break, etc.
//
// This file handles:
//   NODE_WRITE    — print values to stdout (the `write` statement)
//   NODE_IF       — if/else branching; also used for `if cond | loop { }` (while loop)
//   NODE_LOOP     — counted, conditional, or infinite loops
//   NODE_CONTINUE — jump to the next loop iteration
//   NODE_BREAK    — exit the current loop
//   NODE_PROGRAM  — the top-level program (a list of statements)
//   NODE_BLOCK    — a block of statements { ... }
//
// Variable declarations (NODE_LET, NODE_EDIT) live in var_gen.cpp.
// Functions (NODE_FN, NODE_CALL, NODE_RETURN) live in fn_gen.cpp.
//
// How LLVM control flow works:
//   Code is organized into "basic blocks". Each block ends with a "terminator"
//   instruction (branch, conditional branch, or return). When we generate an
//   if/else, we create three blocks: "then", "else", and "merge" (where both
//   paths rejoin).

#include "context.hpp"
#include "helpers.hpp"

using namespace llvm;

// Forward declarations for functions defined in their own files
Value* gen_let(Node *node);
Value* gen_edit(Node *node);
Value* gen_fn(Node *node);
Value* gen_call(Node *node);
Value* gen_return(Node *node);

// ---------------------------------------------------------------------------
// NODE_WRITE — print values to stdout
// ---------------------------------------------------------------------------

// Handles:  write x, y, z
// Emits calls to the C `printf` function with the appropriate format string
// for each value's type.
static Value* gen_write(Node *node) {
    Function *printf_fn = get_or_declare_printf();

    // Determine what to print at the end of the line.
    // Default is a newline "\n"; can be overridden by a keyword modifier.
    const char *end_str = "\n";

    if (node->write.modifier_sym != 0) {
        // Check if the user set a custom modifier (e.g. `write.edit { end: "" }`)
        auto it = KeywordModifiers.find(node->write.modifier_sym);
        if (it != KeywordModifiers.end()) {
            const char *val = get_keyword_attr(it->second, "end");
            if (val) end_str = val;
        } else {
            // Fall back to the keyword's default value for "end"
            std::string kw_name = sym_name(node->write.modifier_sym);
            KeywordMeta *meta = get_keyword_meta_by_name(kw_name.c_str());
            if (meta) {
                for (uint32_t a = 0; a < meta->attr_count; a++) {
                    if (strcmp(meta->attributes[a].name, "end") == 0) {
                        end_str = meta->attributes[a].default_value;
                        break;
                    }
                }
            }
        }
    }

    // Emit a printf call for each argument with the right format string
    for (uint32_t i = 0; i < node->write.count; i++) {
        Node  *arg = node->write.exprs[i];
        Value *val = expr_gen(arg);
        if (!val) continue;

        Type *ty = val->getType();

        if (ty->isPointerTy() || arg->kind == NODE_STRING_LIT) {
            // String: printf("%s", val)
            Value *fmt = Builder->CreateGlobalString("%s", "fmt.str");
            Builder->CreateCall(printf_fn, { fmt, val });

        } else if (ty->isDoubleTy()) {
            // Double: printf("%f", val)
            Value *fmt = Builder->CreateGlobalString("%f", "fmt.f64");
            Builder->CreateCall(printf_fn, { fmt, val });

        } else if (ty->isIntegerTy(1)) {
            // Bool: select between "true" and "false" strings
            Value *fmt  = Builder->CreateGlobalString("%s",    "fmt.bool");
            Value *tstr = Builder->CreateGlobalString("true",  "str.true");
            Value *fstr = Builder->CreateGlobalString("false", "str.false");
            Value *sel  = Builder->CreateSelect(val, tstr, fstr, "bool.str");
            Builder->CreateCall(printf_fn, { fmt, sel });

        } else if (ty->isIntegerTy(64)) {
            // 64-bit integer: printf("%lld", val)
            Value *fmt = Builder->CreateGlobalString("%lld", "fmt.i64");
            Builder->CreateCall(printf_fn, { fmt, val });

        } else if (ty->isIntegerTy()) {
            // Smaller integer: cast to i32 and use %d
            Value *fmt  = Builder->CreateGlobalString("%d", "fmt.int");
            Value *cast = Builder->CreateIntCast(val, Builder->getInt32Ty(), /*isSigned=*/true);
            Builder->CreateCall(printf_fn, { fmt, cast });

        } else {
            fprintf(stderr, "Compiler Error: write: unhandled type for argument %u\n", i);
        }
    }

    // Print the end character(s) (default: newline)
    Value *end_val = Builder->CreateGlobalString(end_str, "write.end");
    Builder->CreateCall(printf_fn, { end_val });

    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// ---------------------------------------------------------------------------
// NODE_IF (while-loop variant) — `if cond | loop { body }`
// ---------------------------------------------------------------------------

// Emits a while loop: repeatedly evaluate `cond`; run `body` while it's true.
//
// Block layout:
//   [current] → loop.cond → loop.body → (back to loop.cond)
//                        ↘ loop.exit
static Value* gen_while_loop(Node *node) {
    Function *fn = Builder->GetInsertBlock()->getParent();

    BasicBlock *cond_bb = BasicBlock::Create(*TheContext, "loop.cond", fn);
    BasicBlock *body_bb = BasicBlock::Create(*TheContext, "loop.body", fn);
    BasicBlock *exit_bb = BasicBlock::Create(*TheContext, "loop.exit", fn);

    // Jump into the condition check block
    Builder->CreateBr(cond_bb);

    // Emit the condition
    Builder->SetInsertPoint(cond_bb);
    Value *cond = expr_gen(node->if_.cond);
    if (!cond) return nullptr;
    Builder->CreateCondBr(ensure_bool(cond, "loopcond"), body_bb, exit_bb);

    // Emit the loop body; push loop so break/continue work
    LoopStack.push_back({cond_bb, exit_bb});
    Builder->SetInsertPoint(body_bb);
    stmt_gen(node->if_.then_);
    if (!Builder->GetInsertBlock()->getTerminator())
        Builder->CreateBr(cond_bb);  // loop back
    LoopStack.pop_back();

    // Continue after the loop
    Builder->SetInsertPoint(exit_bb);
    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// ---------------------------------------------------------------------------
// NODE_IF — if/else branching
// ---------------------------------------------------------------------------

// Emits an if/else:
//
//   Block layout:
//     [current] → then → merge
//               ↘ else → merge
static Value* gen_if(Node *node) {
    // `if cond | loop { body }` is actually a while loop in disguise
    if (node->if_.is_loop)
        return gen_while_loop(node);

    Function *fn = Builder->GetInsertBlock()->getParent();

    // Evaluate the condition
    Value *cond = expr_gen(node->if_.cond);
    if (!cond) return nullptr;

    BasicBlock *then_bb  = BasicBlock::Create(*TheContext, "then",  fn);
    BasicBlock *else_bb  = BasicBlock::Create(*TheContext, "else",  fn);
    BasicBlock *merge_bb = BasicBlock::Create(*TheContext, "merge", fn);

    // Branch to then or else
    Builder->CreateCondBr(
        ensure_bool(cond, "ifcond"),
        then_bb,
        node->if_.else_ ? else_bb : merge_bb
    );

    // Emit the "then" branch
    Builder->SetInsertPoint(then_bb);
    stmt_gen(node->if_.then_);
    if (!Builder->GetInsertBlock()->getTerminator())
        Builder->CreateBr(merge_bb);

    // Emit the "else" branch (or just jump to merge if no else)
    Builder->SetInsertPoint(else_bb);
    if (node->if_.else_) {
        stmt_gen(node->if_.else_);
        if (!Builder->GetInsertBlock()->getTerminator())
            Builder->CreateBr(merge_bb);
    } else {
        Builder->CreateBr(merge_bb);
    }

    // Continue after the if/else
    Builder->SetInsertPoint(merge_bb);
    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// Helper: generate a type-aware equality comparison between two values.
//
// Handles three cases:
//   - Strings (pointer types): call strcmp, check result == 0
//   - Floats (double): use LLVM ordered float-equal comparison
//   - Integers: widen to the larger bit-width then use integer equal comparison
static Value* create_eq_comparison(Value *L, Value *R) {
    if (!L || !R) return nullptr;

    // String comparison: any pointer operand → use strcmp
    // icmp eq on pointers compares addresses (wrong); strcmp compares content
    if (L->getType()->isPointerTy() || R->getType()->isPointerTy()) {
        Function *strcmp_fn = get_or_declare_strcmp();
        Value *cmp = Builder->CreateCall(strcmp_fn, {L, R}, "strcmp.res");
        // strcmp returns 0 when the strings are equal
        return Builder->CreateICmpEQ(cmp, ConstantInt::get(Builder->getInt32Ty(), 0), "streq");
    }

    // Float comparison: promote both sides to double first
    bool is_float = L->getType()->isDoubleTy() || R->getType()->isDoubleTy();
    if (is_float) {
        L = coerce_value(L, Type::getDoubleTy(*TheContext));
        R = coerce_value(R, Type::getDoubleTy(*TheContext));
        return Builder->CreateFCmpOEQ(L, R, "feq");
    }

    // Integer comparison: widen to the larger bit-width if sizes differ
    if (L->getType() != R->getType() &&
        L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
        Type *wide = L->getType()->getIntegerBitWidth() > R->getType()->getIntegerBitWidth()
                     ? L->getType() : R->getType();
        L = coerce_value(L, wide);
        R = coerce_value(R, wide);
    }
    return Builder->CreateICmpEQ(L, R, "ieq");
}

// ---------------------------------------------------------------------------
// NODE_WHEN — pattern matching switch-like statement
//
// Syntax:
//   when value {
//       1:    { write "one" }     — equality match: value == 1
//       . > 5: { write "big" }   — predicate: value > 5  (dot = subject)
//       _:    { write "other" }  — catch-all default
//   }
//
// Implementation:
//   We chain basic blocks: for each arm we emit a condition check that
//   branches to the arm body on match, or falls to the next arm's check.
//   At the end all paths converge at a single merge block.
// ---------------------------------------------------------------------------
static Value* gen_when(Node *node) {
    Function *fn = Builder->GetInsertBlock()->getParent();

    // Evaluate the subject expression once and keep the value
    Value *subject_val = expr_gen(node->when.subject);
    if (!subject_val) return nullptr;

    BasicBlock *merge_bb     = BasicBlock::Create(*TheContext, "when.merge", fn);
    BasicBlock *curr_cond_bb = Builder->GetInsertBlock();

    // Save and set WhenSubject so that bare '.' nodes inside predicate arm
    // patterns resolve to the subject value
    Value *saved_subject = WhenSubject;
    WhenSubject = subject_val;

    for (uint32_t i = 0; i < node->when.arm_count; i++) {
        Node *arm = node->when.arms[i];

        BasicBlock *body_bb = BasicBlock::Create(*TheContext, "when.body", fn);

        if (arm->when_arm.is_else) {
            // '_' or 'default' arm: jump unconditionally to the body
            Builder->SetInsertPoint(curr_cond_bb);
            Builder->CreateBr(body_bb);

            Builder->SetInsertPoint(body_bb);
            stmt_gen(arm->when_arm.body);
            if (!Builder->GetInsertBlock()->getTerminator())
                Builder->CreateBr(merge_bb);

            curr_cond_bb = nullptr;
            break;

        } else {
            BasicBlock *next_cond_bb = BasicBlock::Create(*TheContext, "when.cond", fn);
            Builder->SetInsertPoint(curr_cond_bb);

            Value *cond_val = nullptr;

            if (arm->when_arm.is_predicate) {
                // Predicate arm: '. op expr' — the pattern IS the condition
                // WhenSubject is already set so the '.' in the pattern resolves
                // to subject_val. Just evaluate and treat as boolean.
                cond_val = expr_gen(arm->when_arm.pattern);
                if (!cond_val) { WhenSubject = saved_subject; return nullptr; }
                cond_val = ensure_bool(cond_val, "whencond");
            } else {
                // Equality arm: pattern == subject
                Value *pattern_val = expr_gen(arm->when_arm.pattern);
                if (!pattern_val) { WhenSubject = saved_subject; return nullptr; }
                Value *eq = create_eq_comparison(subject_val, pattern_val);
                if (!eq) { WhenSubject = saved_subject; return nullptr; }
                cond_val = ensure_bool(eq, "whencond");
            }

            Builder->CreateCondBr(cond_val, body_bb, next_cond_bb);

            // Emit body
            Builder->SetInsertPoint(body_bb);
            stmt_gen(arm->when_arm.body);
            if (!Builder->GetInsertBlock()->getTerminator())
                Builder->CreateBr(merge_bb);

            curr_cond_bb = next_cond_bb;
        }
    }

    // If no arm matched (and there was no default), just fall to merge
    if (curr_cond_bb) {
        Builder->SetInsertPoint(curr_cond_bb);
        Builder->CreateBr(merge_bb);
    }

    // Restore WhenSubject for any outer when statement
    WhenSubject = saved_subject;

    Builder->SetInsertPoint(merge_bb);
    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// ---------------------------------------------------------------------------
// NODE_LOOP — general loop (counted, conditional, or infinite)
// ---------------------------------------------------------------------------

// Handles three loop forms:
//   loop 10 { ... }         — counted loop (runs exactly N times)
//   loop cond { ... }       — conditional loop (like while)
//   loop { ... }            — infinite loop (use `break` to exit)
//
// Block layout for counted loops:
//   [current] → loop.cond → loop.body → loop.inc → (back to loop.cond)
//                        ↘ loop.exit
static Value* gen_loop(Node *node) {
    Function *fn = Builder->GetInsertBlock()->getParent();

    BasicBlock *cond_bb = BasicBlock::Create(*TheContext, "loop.cond", fn);
    BasicBlock *body_bb = BasicBlock::Create(*TheContext, "loop.body", fn);
    BasicBlock *exit_bb = BasicBlock::Create(*TheContext, "loop.exit", fn);

    // For counted loops: allocate a counter variable in the entry block
    AllocaInst *counter = nullptr;
    Value      *limit   = nullptr;

    if (node->loop.count) {
        limit = expr_gen(node->loop.count);
        if (!limit) return nullptr;
        counter = alloca_at_entry(fn, limit->getType(), "loop.i");
        // Initialize counter to 0
        Builder->CreateStore(ConstantInt::get(limit->getType(), 0), counter);
    }

    // For counted loops, `continue` jumps to the increment block (not cond directly)
    BasicBlock *cont_bb = cond_bb;
    BasicBlock *inc_bb  = nullptr;
    if (counter) {
        inc_bb  = BasicBlock::Create(*TheContext, "loop.inc", fn);
        cont_bb = inc_bb;
    }

    // Jump into the condition check
    Builder->CreateBr(cond_bb);

    // Emit the loop condition
    Builder->SetInsertPoint(cond_bb);
    Value *cv = nullptr;

    if (node->loop.cond) {
        // Conditional loop: evaluate the condition expression
        cv = expr_gen(node->loop.cond);
        if (!cv) return nullptr;
        cv = ensure_bool(cv, "loopcond");

    } else if (counter) {
        // Counted loop: check if counter < limit
        Value *cur = Builder->CreateLoad(limit->getType(), counter, "loop.cur");
        cv = Builder->CreateICmpSLT(cur, limit, "loop.lt");

    } else {
        // Infinite loop: condition is always true
        cv = ConstantInt::getTrue(*TheContext);
    }

    Builder->CreateCondBr(cv, body_bb, exit_bb);

    // Emit the loop body
    LoopStack.push_back({cont_bb, exit_bb});  // register break/continue targets
    Builder->SetInsertPoint(body_bb);
    stmt_gen(node->loop.body);
    if (!Builder->GetInsertBlock()->getTerminator())
        Builder->CreateBr(cont_bb);
    LoopStack.pop_back();

    // For counted loops: emit the increment block (counter++)
    if (inc_bb) {
        Builder->SetInsertPoint(inc_bb);
        Value *cur = Builder->CreateLoad(limit->getType(), counter, "loop.cur");
        Value *nxt = Builder->CreateAdd(cur, ConstantInt::get(limit->getType(), 1), "loop.inc");
        Builder->CreateStore(nxt, counter);
        Builder->CreateBr(cond_bb);
    }

    // Continue after the loop
    Builder->SetInsertPoint(exit_bb);
    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// ---------------------------------------------------------------------------
// NODE_CONTINUE / NODE_BREAK — loop control
// ---------------------------------------------------------------------------

// `continue` — jump back to the loop's continue target (top or increment block)
static Value* gen_continue(Node *node) {
    if (LoopStack.empty()) {
        fprintf(stderr, "Compiler Error: 'continue' used outside of a loop\n");
        return nullptr;
    }
    Builder->CreateBr(LoopStack.back().first);

    // Dead block to absorb any code after the continue
    Function   *fn   = Builder->GetInsertBlock()->getParent();
    BasicBlock *dead = BasicBlock::Create(*TheContext, "after.continue", fn);
    Builder->SetInsertPoint(dead);
    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// `break` — jump to the loop's exit block
static Value* gen_break(Node *node) {
    if (LoopStack.empty()) {
        fprintf(stderr, "Compiler Error: 'break' used outside of a loop\n");
        return nullptr;
    }
    Builder->CreateBr(LoopStack.back().second);

    // Dead block to absorb any code after the break
    Function   *fn   = Builder->GetInsertBlock()->getParent();
    BasicBlock *dead = BasicBlock::Create(*TheContext, "after.break", fn);
    Builder->SetInsertPoint(dead);
    return ConstantInt::get(Builder->getInt32Ty(), 0);
}

// ---------------------------------------------------------------------------
// NODE_BLOCK / NODE_PROGRAM — sequences of statements
// ---------------------------------------------------------------------------

// Runs each statement in the block in order and returns the last result.
static Value* gen_block(Node *node) {
    Value *last = nullptr;
    for (uint32_t i = 0; i < node->block.count; i++)
        last = stmt_gen(node->block.stmts[i]);
    return last;
}

// ---------------------------------------------------------------------------
// stmt_gen — main dispatch function
// ---------------------------------------------------------------------------

// Generates LLVM IR for any statement node.
// Returns a Value* (usually ConstantInt 0 for statements), or nullptr on error.
Value* stmt_gen(Node *node) {
    if (!node) return nullptr;

    switch (node->kind) {
        case NODE_LET:      return gen_let(node);
        case NODE_EDIT:     return gen_edit(node);
        case NODE_FN:       return gen_fn(node);
        case NODE_CALL:     return gen_call(node);
        case NODE_RETURN:   return gen_return(node);
        case NODE_WRITE:    return gen_write(node);
        case NODE_IF:       return gen_if(node);
        case NODE_WHEN:     return gen_when(node);
        case NODE_LOOP:     return gen_loop(node);
        case NODE_CONTINUE: return gen_continue(node);
        case NODE_BREAK:    return gen_break(node);
        case NODE_PROGRAM:
        case NODE_BLOCK:    return gen_block(node);

        // Anything else might be an expression used as a statement
        default:
            return expr_gen(node);
    }
}
