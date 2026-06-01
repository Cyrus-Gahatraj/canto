// repl.cpp — REPL (Read-Eval-Print Loop) persistent variable storage
//
// In normal compilation, local variables live on the stack and disappear
// when the function returns. But in the REPL (interactive mode), you need
// variables to persist between each line you type.
//
// This file solves that problem by using a statically allocated global array
// (`ReplStorage`) as a flat memory bank for all REPL variables. Each variable
// gets a slot identified by its symbol ID (a small integer assigned by the parser).
//
// Because the array holds i64 (64-bit integers), we bitcast other types
// (doubles, booleans, pointers) to/from i64 when storing and loading.
//
// The array is exposed to the JIT-compiled code via LLVM's symbol table so
// that generated code can directly read and write slots.

#include "context.hpp"

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Constants.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/Support/Error.h"

#include <cstdio>
#include <cstring>

using namespace llvm;
using namespace llvm::orc;

// The global array that stores REPL variable values as raw i64 bits
GlobalVariable *TheReplGlobals = nullptr;

// ---------------------------------------------------------------------------
// ReplValueType — tag for knowing how to interpret a stored value
// ---------------------------------------------------------------------------

// Each slot in ReplStorage holds an i64, but we need to know the original
// type to print or load it correctly. This enum tracks that.
typedef enum : uint8_t {
    REPL_TYPE_NONE   = 0,  // slot is empty / not yet assigned
    REPL_TYPE_INT    = 1,  // stored as a plain i64
    REPL_TYPE_DOUBLE = 2,  // stored as bit-pattern of a double (via bitcast)
    REPL_TYPE_BOOL   = 3,  // stored as 0 or 1
    REPL_TYPE_STRING = 4,  // stored as a pointer-to-char cast to i64
} ReplValueType;

// ---------------------------------------------------------------------------
// Persistent REPL storage — lives for the duration of the process
// ---------------------------------------------------------------------------

// Raw 64-bit storage for up to 65536 REPL variables (indexed by symbol ID)
static int64_t     ReplStorage[65536] = {};

// Tracks the type of each stored value so we can print/load it correctly
static ReplValueType ReplTypes[65536] = {};

// ---------------------------------------------------------------------------
// repl_init / repl_free — lifecycle
// ---------------------------------------------------------------------------

extern "C" void repl_init(void) {
    IsRepl = true;
    memset(ReplStorage, 0, sizeof(ReplStorage));
    memset(ReplTypes,   0, sizeof(ReplTypes));
}

extern "C" void repl_free(void) {
    IsRepl         = false;
    TheReplGlobals = nullptr;
}

// ---------------------------------------------------------------------------
// repl_setup_globals — create the LLVM global that points to ReplStorage
// ---------------------------------------------------------------------------

// Declares `canto_repl_storage` as a global array in the LLVM module.
// Generated code will read/write this array to access REPL variables.
extern std::unique_ptr<LLJIT> TheJIT;
extern "C" void repl_set_type(uint32_t sym_id, uint8_t type_tag);

extern "C" void repl_setup_globals(void) {
    if (!TheModule) return;

    // LLVM array type: [65536 x i64]
    ArrayType *arr_type = ArrayType::get(Type::getInt64Ty(*TheContext), 65536);

    TheReplGlobals = cast<GlobalVariable>(
        TheModule->getOrInsertGlobal("canto_repl_storage", arr_type)
    );
    // External linkage so the JIT can resolve it to our actual ReplStorage array
    TheReplGlobals->setLinkage(GlobalValue::ExternalLinkage);
}

// ---------------------------------------------------------------------------
// repl_register_storage — tell the JIT where ReplStorage actually lives
// ---------------------------------------------------------------------------

// Registers the address of our C arrays with the JIT so that generated code
// referring to `canto_repl_storage` gets resolved to the real array.
extern "C" void repl_register_storage(void) {
    if (!TheJIT) return;

    static bool registered = false;
    if (registered) return;  // only register once per process
    registered = true;

    SymbolMap symbols;

    // Register the storage array
    symbols[TheJIT->mangleAndIntern("canto_repl_storage")] = {
        ExecutorAddr::fromPtr(ReplStorage),
        JITSymbolFlags::Exported
    };

    // Register the type-tracking callback
    symbols[TheJIT->mangleAndIntern("repl_set_type")] = {
        ExecutorAddr::fromPtr(reinterpret_cast<void*>(repl_set_type)),
        JITSymbolFlags::Exported
    };

    cantFail(TheJIT->getMainJITDylib().define(absoluteSymbols(symbols)));
}

// ---------------------------------------------------------------------------
// repl_set_type — called by generated code to record a variable's type
// ---------------------------------------------------------------------------

// This function is called by the JIT-compiled code every time a variable
// is assigned, so we know how to interpret the raw i64 bits when printing.
extern "C" void repl_set_type(uint32_t sym_id, uint8_t type_tag) {
    if (sym_id < 65536)
        ReplTypes[sym_id] = (ReplValueType)type_tag;
}

// ---------------------------------------------------------------------------
// repl_gep — compute pointer to a slot in the global array
// ---------------------------------------------------------------------------

// Returns a GEP (GetElementPtr) instruction that points to slot[sym_id]
// in the canto_repl_storage array. Used by repl_store and repl_load.
static Value* repl_gep(uint32_t sym_id) {
    if (!TheReplGlobals) return nullptr;
    Value *idx = ConstantInt::get(Type::getInt64Ty(*TheContext), sym_id);
    return Builder->CreateGEP(
        ArrayType::get(Type::getInt64Ty(*TheContext), 65536),
        TheReplGlobals,
        { ConstantInt::get(Type::getInt32Ty(*TheContext), 0), idx },
        "repl.slot"
    );
}

// ---------------------------------------------------------------------------
// repl_store — write a value into the REPL storage array
// ---------------------------------------------------------------------------

// Called when a variable is assigned in REPL mode.
// Converts the value to i64 bits (with bitcast for doubles, zext for bools,
// ptrtoint for strings), stores it, and records the type tag.
Value* repl_store(uint32_t sym_id, Value *val) {
    Value *gep = repl_gep(sym_id);
    if (!gep) return val;  // can't store, but still return the value

    Value      *to_store = val;
    ReplValueType tag    = REPL_TYPE_INT;

    if (val->getType()->isDoubleTy()) {
        // Reinterpret double bits as i64 (no value change, just type change)
        to_store = Builder->CreateBitOrPointerCast(val, Type::getInt64Ty(*TheContext), "f64.bits");
        tag = REPL_TYPE_DOUBLE;

    } else if (val->getType()->isIntegerTy(1)) {
        // Normalize bool: i1 → i1 (ensure it's 0 or 1), then zero-extend to i64
        Value *norm = Builder->CreateICmpNE(val, ConstantInt::get(val->getType(), 0), "bool.norm");
        to_store = Builder->CreateZExt(norm, Type::getInt64Ty(*TheContext), "bool.zext");
        tag = REPL_TYPE_BOOL;

    } else if (val->getType()->isPointerTy()) {
        // Store string pointer as integer (pointer → i64)
        to_store = Builder->CreatePtrToInt(val, Type::getInt64Ty(*TheContext), "ptr.int");
        tag = REPL_TYPE_STRING;

    } else if (!val->getType()->isIntegerTy(64)) {
        // Other integer types: sign-extend to i64
        to_store = Builder->CreateIntCast(val, Type::getInt64Ty(*TheContext), /*isSigned=*/true, "int.cast");
        tag = REPL_TYPE_INT;
    }

    Builder->CreateStore(to_store, gep);

    // Tell the runtime what type this slot holds (for repl_print/repl_load)
    Function *set_type_fn = TheModule->getFunction("repl_set_type");
    if (!set_type_fn) {
        FunctionType *ft = FunctionType::get(
            Type::getVoidTy(*TheContext),
            { Type::getInt32Ty(*TheContext), Type::getInt8Ty(*TheContext) },
            /*isVarArg=*/false
        );
        set_type_fn = Function::Create(ft, Function::ExternalLinkage, "repl_set_type", TheModule.get());
    }
    Builder->CreateCall(set_type_fn, {
        ConstantInt::get(Type::getInt32Ty(*TheContext), sym_id),
        ConstantInt::get(Type::getInt8Ty(*TheContext),  (uint8_t)tag)
    });

    return val;
}

// ---------------------------------------------------------------------------
// repl_load — read a value from the REPL storage array
// ---------------------------------------------------------------------------

// Loads the raw i64 from the slot, then converts it back to the correct type
// based on the type tag recorded at the last store.
Value* repl_load(uint32_t sym_id) {
    Value *gep = repl_gep(sym_id);
    if (!gep) return nullptr;

    // Load the raw 64-bit integer from the slot
    Value *raw = Builder->CreateLoad(Type::getInt64Ty(*TheContext), gep, "repl.raw");

    ReplValueType tag = ReplTypes[sym_id];

    switch (tag) {
        case REPL_TYPE_DOUBLE:
            // Reinterpret the i64 bits back as a double
            return Builder->CreateBitOrPointerCast(raw, Type::getDoubleTy(*TheContext), "f64.restore");

        case REPL_TYPE_BOOL:
            // Truncate to i1 (the high bits were zeroed when stored)
            return Builder->CreateTrunc(raw, Type::getInt1Ty(*TheContext), "bool.restore");

        case REPL_TYPE_STRING:
            // Convert i64 back to a char* pointer
            return Builder->CreateIntToPtr(
                raw,
                PointerType::get(*TheContext, 0),
                "ptr.restore"
            );

        default:
            // Integer — return raw i64 as-is
            return raw;
    }
}

// ---------------------------------------------------------------------------
// repl_print — print the current value of a variable (called from Rust side)
// ---------------------------------------------------------------------------

// Prints the stored value for a symbol ID to stdout.
// Called by the REPL driver after each evaluation to show the result.
extern "C" void repl_print(uint32_t sym_id) {
    if (sym_id >= 65536) return;

    int64_t      raw = ReplStorage[sym_id];
    ReplValueType tag = ReplTypes[sym_id];

    switch (tag) {
        case REPL_TYPE_INT:
            printf("%lld\n", (long long)raw);
            break;

        case REPL_TYPE_DOUBLE: {
            // Reinterpret raw bits as a double
            double d;
            memcpy(&d, &raw, sizeof(double));
            printf("%g\n", d);
            break;
        }

        case REPL_TYPE_BOOL:
            printf("%s\n", raw ? "true" : "false");
            break;

        case REPL_TYPE_STRING:
            printf("\"%s\"\n", (const char*)(uintptr_t)raw);
            break;

        default:
            break;  // REPL_TYPE_NONE — nothing to print
    }

    fflush(stdout);
}
