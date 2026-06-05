// helpers.cpp — Implementations of shared codegen utility functions
//
// See helpers.hpp for the public interface and documentation.
// These are the workhorses that every other codegen file calls to avoid
// repeating the same LLVM patterns over and over.

#include "helpers.hpp"
#include <cstring>

using namespace llvm;

// ---------------------------------------------------------------------------
// alloca_at_entry
// ---------------------------------------------------------------------------

// Creates a stack slot for a local variable in the function's entry block.
//
// Why entry block? LLVM's optimizer (mem2reg pass) can only promote allocas
// to registers when they are in the entry block. Putting allocas elsewhere
// produces correct but slower code.
AllocaInst* alloca_at_entry(Function *fn, Type *type, const std::string &name) {
    // Create a temporary builder pointing at the very start of the entry block
    IRBuilder<> entry_builder(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return entry_builder.CreateAlloca(type, nullptr, name);
}

// ---------------------------------------------------------------------------
// coerce_value
// ---------------------------------------------------------------------------

// Converts `val` to `target_type` when they differ.
// This handles the common numeric promotion/demotion cases:
//   int   → double  : sign-extend int to floating point
//   double → int    : truncate floating point to integer (lossy)
//   int   → int     : resize (e.g. i1 → i64 or i64 → i32)
// If the types already match, returns val unchanged.
// Returns nullptr for unsupported conversions.
Value* coerce_value(Value *val, Type *target_type) {
    if (!val || !target_type) return nullptr;

    Type *src = val->getType();
    if (src == target_type) return val;

    // int → double
    if (target_type->isDoubleTy() && src->isIntegerTy())
        return Builder->CreateSIToFP(val, target_type, "to_f64");

    // double → int
    if (target_type->isIntegerTy() && src->isDoubleTy())
        return Builder->CreateFPToSI(val, target_type, "to_int");

    // int → int (different bit widths)
    if (target_type->isIntegerTy() && src->isIntegerTy())
        return Builder->CreateIntCast(val, target_type, /*isSigned=*/true, "int_cast");

    // Unsupported — caller decides what to do
    return nullptr;
}

// ---------------------------------------------------------------------------
// ensure_bool
// ---------------------------------------------------------------------------

// Makes sure `val` is a 1-bit integer (i1) suitable for conditional branches.
//
// LLVM branches require an i1 operand. If the value is already i1 (the result
// of a comparison like ==, <, etc.) we return it as-is. Otherwise we generate
// the equivalent of "val != 0" which always yields i1.
Value* ensure_bool(Value *val, const std::string &name) {
    if (!val) return nullptr;

    // Already a boolean — nothing to do
    if (val->getType()->isIntegerTy(1)) return val;

    // Generate: val != 0
    return Builder->CreateICmpNE(
        val,
        ConstantInt::get(val->getType(), 0),
        name
    );
}

// ---------------------------------------------------------------------------
// get_or_declare_printf
// ---------------------------------------------------------------------------

// Returns the `printf` function from the current module, declaring it first
// if it hasn't been declared yet.
//
// C signature: int printf(const char *fmt, ...)
// LLVM IR:     i32 printf(i8*, ...)
//
// Calling this is safe even if called many times — LLVM deduplicates it.
Function* get_or_declare_printf() {
    // Check if printf was already added to the module
    Function *printf_fn = TheModule->getFunction("printf");
    if (printf_fn) return printf_fn;

    // Declare printf: takes a char* (i8*) as first arg, variadic rest
    PointerType *char_ptr = PointerType::get(*TheContext, 0);
    FunctionType *printf_type = FunctionType::get(
        Builder->getInt32Ty(),  // return type: int
        char_ptr,               // first param: const char*
        /*isVarArg=*/true       // accepts additional arguments like %d, %s, etc.
    );

    return Function::Create(
        printf_type,
        Function::ExternalLinkage,  // links to the system printf
        "printf",
        TheModule.get()
    );
}

// ---------------------------------------------------------------------------
// get_or_declare_strcmp
// ---------------------------------------------------------------------------

// Returns the `strcmp` function from the current module, declaring it first
// if it hasn't been declared yet.
//
// C signature: int strcmp(const char *s1, const char *s2)
// LLVM IR:     i32 strcmp(i8*, i8*)
//
// We use this to compare strings in `when` equality arms, because pointer
// equality (icmp eq) would compare addresses, not content.
Function* get_or_declare_strcmp() {
    Function *fn = TheModule->getFunction("strcmp");
    if (fn) return fn;

    PointerType *char_ptr = PointerType::get(*TheContext, 0);
    FunctionType *strcmp_type = FunctionType::get(
        Builder->getInt32Ty(),      // return type: int (0 = equal)
        { char_ptr, char_ptr },     // two const char* parameters
        /*isVarArg=*/false
    );

    return Function::Create(
        strcmp_type,
        Function::ExternalLinkage,  // links to the system strcmp
        "strcmp",
        TheModule.get()
    );
}

// ---------------------------------------------------------------------------
// resolve_edit_attr_name
// ---------------------------------------------------------------------------

// Figures out which attribute name an edit pair is targeting.
//
// An edit pair looks like:  { end: "\n" }
//                              ^^^— this is the attribute name
//
// If the pair has an explicit field_sym (a named field), we use that.
// If not and the keyword has at least one attribute, we default to the first.
// Returns empty string if neither is available.
std::string resolve_edit_attr_name(Node *pair, KeywordMeta *meta) {
    if (!pair || !TheSymtable) return "";

    // Named field: { end: "\n" }
    if (pair->edit_pair.field_sym != 0) {
        const Symbol *s = &TheSymtable->syms[pair->edit_pair.field_sym];
        std::string name(s->start, s->length);
        if (!name.empty() && name[0] == '.') {
            return name.substr(1);
        }
        return name;
    }

    // Positional: use the first attribute as default
    if (meta && meta->attr_count > 0)
        return meta->attributes[0].name;

    return "";
}

// ---------------------------------------------------------------------------
// resolve_edit_attr_value
// ---------------------------------------------------------------------------

// Extracts the string value from an edit pair's value expression.
//
// Handles three value kinds:
//   NODE_STRING_LIT  → the literal string text
//   NODE_IDENT       → the identifier name (treated as a string value)
//   NODE_BOOL_LIT    → "true" or "false"
//
// Returns empty string for any other node kind.
std::string resolve_edit_attr_value(Node *pair) {
    if (!pair || !TheSymtable) return "";

    Node *value_node = pair->edit_pair.value;
    if (!value_node) return "";

    if (value_node->kind == NODE_STRING_LIT) {
        const Symbol *s = &TheSymtable->syms[value_node->string_lit.sym];
        return std::string(s->start, s->length);
    }

    if (value_node->kind == NODE_IDENT) {
        const Symbol *s = &TheSymtable->syms[value_node->ident.sym];
        return std::string(s->start, s->length);
    }

    if (value_node->kind == NODE_BOOL_LIT)
        return value_node->bool_lit.value ? "true" : "false";

    return "";
}

// ---------------------------------------------------------------------------
// keyword_name_to_llvm_type
// ---------------------------------------------------------------------------

// Maps a Canto type name string to the corresponding LLVM Type*.
//
// Canto type names → LLVM types:
//   "int" / "integer" → i64  (64-bit signed integer)
//   "double" / "float" → f64  (64-bit floating point)
//   "bool" / "boolean" → i1   (1-bit boolean)
//   "string"            → i8* (pointer to characters)
//
// Returns nullptr if the name is not a recognized type.
Type* keyword_name_to_llvm_type(const char *name) {
    if (!name) return nullptr;

    if (strcmp(name, "integer") == 0 || strcmp(name, "int") == 0)
        return Type::getInt64Ty(*TheContext);

    if (strcmp(name, "double") == 0 || strcmp(name, "float") == 0)
        return Type::getDoubleTy(*TheContext);

    if (strcmp(name, "bool") == 0 || strcmp(name, "boolean") == 0)
        return Type::getInt1Ty(*TheContext);

    if (strcmp(name, "string") == 0)
        return PointerType::get(*TheContext, 0);

    return nullptr;
}
