// var_gen.cpp — LLVM IR generation for variable declarations and mutations
//
// This file handles two statement kinds:
//
//   NODE_LET  — variable declaration:  let x = 42
//   NODE_EDIT — variable mutation:     x.edit { value: 10 }
//
// In LLVM, local variables are stored as "alloca" instructions (stack slots).
// Writing to a variable = CreateStore, reading from it = CreateLoad.
//
// This file is separate from stmt_gen.cpp to keep each file focused and small.

#include "context.hpp"
#include "helpers.hpp"

using namespace llvm;

// ---------------------------------------------------------------------------
// Keyword instance helper — build a keyword config from an edit block
// ---------------------------------------------------------------------------

// Applies all key-value pairs from an edit node to a keyword instance.
// Used when code like  `let x = write.edit { end: "" }`  is compiled.
static void apply_edit_pairs_to_instance(
    KeywordInstance *inst,
    Node *edit_node,
    KeywordMeta *meta
) {
    for (uint32_t i = 0; i < edit_node->edit.pair_count; i++) {
        Node *pair = edit_node->edit.pairs[i];
        std::string attr = resolve_edit_attr_name(pair, meta);
        if (attr.empty()) continue;

        Node *value_node = pair->edit_pair.value;
        if (!value_node) continue;
        if (value_node->kind != NODE_STRING_LIT &&
            value_node->kind != NODE_IDENT &&
            value_node->kind != NODE_BOOL_LIT) {
            continue;
        }

        std::string val = resolve_edit_attr_value(pair);
        apply_keyword_edit(inst, attr.c_str(), val.c_str());
    }
}

// ---------------------------------------------------------------------------
// NODE_LET — variable declaration
// ---------------------------------------------------------------------------

// Handles:  let name = value
//   1. If the value is a keyword.edit block → store a KeywordInstance, not an LLVM value
//   2. Otherwise evaluate the initializer expression
//   3. Look up any type annotation on the variable
//   4. Allocate a stack slot and store the value
Value* gen_let(Node *node) {
    // --- Special case: `let x = SomeKeyword.edit { ... }` ---
    // This does NOT produce LLVM IR — it creates a runtime config object.
    if (node->let.value && node->let.value->kind == NODE_EDIT) {
        Node *target = node->let.value->edit.target;
        KeywordMeta *meta = nullptr;
        uint32_t tk_type = 0;

        if (target->kind == NODE_KEYWORD) {
            // Edit target is a built-in keyword token
            tk_type = target->keyword.tk_type;
            meta = get_keyword_meta(tk_type);
        } else if (target->kind == NODE_IDENT) {
            // Edit target is a named keyword (looked up by name)
            std::string tname = sym_name(target->ident.sym);
            meta = get_keyword_meta_by_name(tname.c_str());
            if (meta) tk_type = meta->tk_type;
        }

        if (meta) {
            // Create a keyword instance and fill in the attribute values
            KeywordInstance *inst = create_keyword_instance(tk_type);
            apply_edit_pairs_to_instance(inst, node->let.value, meta);
            KeywordModifiers[node->let.name_sym] = inst;
            return ConstantInt::get(Builder->getInt32Ty(), 0);
        }
    }

    // --- Normal variable declaration ---

    // Evaluate the initializer expression to get the initial value
    Value *val = expr_gen(node->let.value);
    if (!val) return nullptr;

    std::string var_name = sym_name(node->let.name_sym);

    // If the initializer is an array, record its element type for later indexing
    if (node->let.value && node->let.value->kind == NODE_ARRAY
        && node->let.value->array.count > 0) {
        Value *first = expr_gen(node->let.value->array.exprs[0]);
        if (first)
            VariableElementTypes[var_name] = first->getType();
    }

    // --- Check for a type annotation (e.g. `let x: MyType = ...`) ---
    // Type annotations in Canto are expressed using keyword modifiers.
    Type *declared_type = nullptr;
    bool  is_many       = false;  // "is_many" means the type is a pointer/array

    if (node->let.type_ann && node->let.type_ann->kind == NODE_IDENT) {
        auto it = KeywordModifiers.find(node->let.type_ann->ident.sym);
        if (it != KeywordModifiers.end()) {
            // Read the "name" attribute to get the Canto type name
            const char *name_val    = get_keyword_attr(it->second, "name");
            const char *is_many_val = get_keyword_attr(it->second, "is_many");

            if (is_many_val && strcmp(is_many_val, "true") == 0)
                is_many = true;

            if (name_val)
                declared_type = keyword_name_to_llvm_type(name_val);
        }
    }

    // Apply the declared type if we found one
    if (declared_type) {
        if (is_many) {
            // "is_many" → variable holds a pointer to an array of that element type
            VariableElementTypes[var_name] = declared_type;
            declared_type = PointerType::get(*TheContext, 0);
        }
        // Coerce the value to the declared type if necessary
        Value *coerced = coerce_value(val, declared_type);
        if (coerced) val = coerced;
    }

    // --- In REPL mode, store into the persistent global array instead ---
    if (IsRepl && TheReplGlobals)
        return repl_store(node->let.name_sym, val);

    // --- Normal mode: allocate a stack slot and store the value ---
    Function *fn = Builder->GetInsertBlock()->getParent();
    Type *slot_type = declared_type ? declared_type : val->getType();
    AllocaInst *slot = alloca_at_entry(fn, slot_type, var_name);

    Builder->CreateStore(val, slot);
    NamedValues[var_name] = slot;

    return val;
}

// ---------------------------------------------------------------------------
// NODE_EDIT — variable mutation / keyword configuration
// ---------------------------------------------------------------------------

// Handles:  x.edit { value: 10 }
//   For variables: updates the value stored in the alloca slot
//   For keywords:  updates the keyword's configuration instance
Value* gen_edit(Node *node) {
    Node *target = node->edit.target;

    // --- Case 1: editing a keyword (e.g. `write.edit { end: "" }`) ---
    if (target->kind == NODE_KEYWORD) {
        uint32_t tk_type = target->keyword.tk_type;
        KeywordMeta *meta = get_keyword_meta(tk_type);
        if (!meta) {
            fprintf(stderr, "Compiler Error: Unknown keyword type for edit\n");
            return nullptr;
        }

        // Intern the keyword name as a symbol to use as the map key
        Symbol kw_sym;
        kw_sym.start  = meta->name;
        kw_sym.length = (uint32_t)strlen(meta->name);
        symtable_hash(&kw_sym);
        SymId kw_id = intern_symbol(TheSymtable, &kw_sym);

        // Get or create the keyword instance for this keyword
        auto it = KeywordModifiers.find(kw_id);
        KeywordInstance *inst;
        if (it != KeywordModifiers.end()) {
            inst = it->second;
        } else {
            inst = create_keyword_instance(tk_type);
            KeywordModifiers[kw_id] = inst;
        }

        // Apply each attribute update
        apply_edit_pairs_to_instance(inst, node, meta);
        return ConstantInt::get(Builder->getInt32Ty(), 0);
    }

    // --- Case 2: editing a variable (must be an identifier) ---
    if (target->kind != NODE_IDENT) {
        fprintf(stderr, "Compiler Error: Edit target must be an identifier or keyword\n");
        return nullptr;
    }

    std::string name = sym_name(target->ident.sym);
    auto it = NamedValues.find(name);
    if (it == NamedValues.end()) {
        fprintf(stderr, "Compiler Error: Undefined variable '%s' in edit\n", name.c_str());
        return nullptr;
    }
    AllocaInst *slot = it->second;

    // Process each value update in the edit block
    for (uint32_t i = 0; i < node->edit.pair_count; i++) {
        Node *pair = node->edit.pairs[i];

        if (pair->edit_pair.value->kind == NODE_RELATIVE) {
            // Relative update: x.edit { value: += 5 }
            // Read the current value, apply the operation, write it back
            Node  *rel     = pair->edit_pair.value;
            Value *current = Builder->CreateLoad(slot->getAllocatedType(), slot, "edit_cur");
            Value *operand = expr_gen(rel->relative.expr);
            if (!operand) return nullptr;

            // Promote to double if either side is a double
            bool fp = current->getType()->isDoubleTy() || operand->getType()->isDoubleTy();
            if (fp) {
                current = coerce_value(current, Type::getDoubleTy(*TheContext));
                operand = coerce_value(operand, Type::getDoubleTy(*TheContext));
            }

            Value *result = nullptr;
            switch (rel->relative.op) {
                case TK_PLUS:
                    result = fp ? Builder->CreateFAdd(current, operand, "edit_add")
                                : Builder->CreateAdd (current, operand, "edit_add");
                    break;
                case TK_MINUS:
                    result = fp ? Builder->CreateFSub(current, operand, "edit_sub")
                                : Builder->CreateSub (current, operand, "edit_sub");
                    break;
                case TK_STAR:
                    result = fp ? Builder->CreateFMul(current, operand, "edit_mul")
                                : Builder->CreateMul (current, operand, "edit_mul");
                    break;
                case TK_SLASH:
                    result = fp ? Builder->CreateFDiv(current, operand, "edit_div")
                                : Builder->CreateSDiv(current, operand, "edit_div");
                    break;
                default:
                    fprintf(stderr, "Compiler Error: Unknown relative operator in edit\n");
                    return nullptr;
            }
            Builder->CreateStore(result, slot);

        } else {
            // Absolute update: x.edit { value: 42 }
            // Evaluate the new value and store it directly
            Value *val = expr_gen(pair->edit_pair.value);
            if (!val) return nullptr;

            // Coerce if the types differ
            if (val->getType() != slot->getAllocatedType()) {
                Value *coerced = coerce_value(val, slot->getAllocatedType());
                if (coerced) val = coerced;
            }

            Builder->CreateStore(val, slot);
        }
    }

    // An edit expression evaluates to the final value of the variable
    return Builder->CreateLoad(slot->getAllocatedType(), slot, "edit_result");
}
