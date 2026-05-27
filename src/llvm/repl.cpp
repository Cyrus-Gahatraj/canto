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

GlobalVariable *TheReplGlobals = nullptr;

typedef enum : uint8_t {
    REPL_TYPE_NONE   = 0,
    REPL_TYPE_INT    = 1,
    REPL_TYPE_DOUBLE = 2,
    REPL_TYPE_BOOL   = 3,
    REPL_TYPE_STRING = 4,
} ReplType;

static int64_t  ReplStorage[65536] = {};
static ReplType ReplTypes[65536] = {};

extern "C" void repl_init(void) {
    IsRepl = true;
    memset(ReplStorage, 0, sizeof(ReplStorage));
    memset(ReplTypes,   0, sizeof(ReplTypes));
}

extern "C" void repl_free(void) {
    IsRepl         = false;
    TheReplGlobals = nullptr;
}

extern std::unique_ptr<LLJIT> TheJIT;

extern "C" void repl_set_type(uint32_t sym_id, uint8_t type_tag);

extern "C" void repl_setup_globals(void) {
    if (!TheModule) return;

    ArrayType *arr_type =
        ArrayType::get(Type::getInt64Ty(*TheContext), 65536);

    TheReplGlobals = cast<GlobalVariable>(
        TheModule->getOrInsertGlobal("canto_repl_storage", arr_type)
    );

    TheReplGlobals->setLinkage(GlobalValue::ExternalLinkage);
}

extern "C" void repl_register_storage(void) {
    if (!TheJIT) return;

    static bool registered = false;
    if (registered) return;
    registered = true;

    SymbolMap symbols;

    auto storage_addr = ExecutorAddr::fromPtr(ReplStorage);
    symbols[TheJIT->mangleAndIntern("canto_repl_storage")] = {
        storage_addr, JITSymbolFlags::Exported
    };

    auto set_type_addr = ExecutorAddr::fromPtr(
        reinterpret_cast<void*>(repl_set_type));
    symbols[TheJIT->mangleAndIntern("repl_set_type")] = {
        set_type_addr, JITSymbolFlags::Exported
    };

    cantFail(
        TheJIT->getMainJITDylib().define(absoluteSymbols(symbols))
    );
}

extern "C" void repl_set_type(uint32_t sym_id, uint8_t type_tag) {
    if (sym_id < 65536)
        ReplTypes[sym_id] = (ReplType)type_tag;
}

Value *repl_gep(uint32_t sym_id) {
    if (!TheReplGlobals) return nullptr;
    Value *idx = ConstantInt::get(Type::getInt64Ty(*TheContext), sym_id);
    return Builder->CreateGEP(
        ArrayType::get(Type::getInt64Ty(*TheContext), 65536),
        TheReplGlobals,
        {ConstantInt::get(Type::getInt32Ty(*TheContext), 0), idx},
        "repl_slot");
}

Value *repl_store(uint32_t sym_id, Value *val) {
    Value *gep = repl_gep(sym_id);
    if (!gep) return val;

    Value *to_store = val;
    ReplType tag    = REPL_TYPE_INT;

    if (val->getType()->isDoubleTy()) {
        to_store = Builder->CreateBitOrPointerCast(
            val, Type::getInt64Ty(*TheContext), "f64_bits");
        tag = REPL_TYPE_DOUBLE;
    } else if (val->getType()->isIntegerTy(1)) {
        Value *norm = Builder->CreateICmpNE(
            val,
            ConstantInt::get(val->getType(), 0),
            "bool_norm"
        );
        to_store = Builder->CreateZExt(
            norm,
            Type::getInt64Ty(*TheContext),
            "bool_zext"
        );
        tag = REPL_TYPE_BOOL;
    } else if (val->getType()->isPointerTy()) {
        to_store = Builder->CreatePtrToInt(
            val, Type::getInt64Ty(*TheContext), "ptr_int");
        tag = REPL_TYPE_STRING;
    } else if (!val->getType()->isIntegerTy(64)) {
        to_store = Builder->CreateIntCast(
            val, Type::getInt64Ty(*TheContext), true, "int_cast");
        tag = REPL_TYPE_INT;
    }

    Builder->CreateStore(to_store, gep);

    Function *set_type_fn = TheModule->getFunction("repl_set_type");
    if (!set_type_fn) {
        FunctionType *ft = FunctionType::get(
            Type::getVoidTy(*TheContext),
            {Type::getInt32Ty(*TheContext), Type::getInt8Ty(*TheContext)},
            false);
        set_type_fn = Function::Create(
            ft, Function::ExternalLinkage, "repl_set_type", TheModule.get());
    }
    Builder->CreateCall(set_type_fn, {
        ConstantInt::get(Type::getInt32Ty(*TheContext), sym_id),
        ConstantInt::get(Type::getInt8Ty(*TheContext),  (uint8_t)tag)
    });

    return val;
}

Value *repl_load(uint32_t sym_id) {
    Value *gep = repl_gep(sym_id);
    if (!gep) return nullptr;

    Value *raw = Builder->CreateLoad(
        Type::getInt64Ty(*TheContext), gep, "repl_raw");

    ReplType tag = ReplTypes[sym_id];

    switch (tag) {
        case REPL_TYPE_DOUBLE:
            return Builder->CreateBitOrPointerCast(
                raw, Type::getDoubleTy(*TheContext), "f64_restore");
        case REPL_TYPE_BOOL:
            return Builder->CreateTrunc(
                raw, Type::getInt1Ty(*TheContext), "bool_restore");
        case REPL_TYPE_STRING:
            return Builder->CreateIntToPtr(
                raw, PointerType::get(Type::getInt8Ty(*TheContext), 0),
                "ptr_restore");
        default:
            return raw;
    }
}

extern "C" void repl_print(uint32_t sym_id) {
    if (sym_id >= 65536) return;
    int64_t raw = ReplStorage[sym_id];
    ReplType tag = ReplTypes[sym_id];

    switch (tag) {
        case REPL_TYPE_INT:
            printf("%lld\n", (long long)raw);
            break;
        case REPL_TYPE_DOUBLE: {
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
            break;
    }
    fflush(stdout);
}
