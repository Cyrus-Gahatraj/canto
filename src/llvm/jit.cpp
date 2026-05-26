#include "canto/jit.h"
#include "context.hpp"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Error.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Constants.h"
#include <cstdio>
#include <memory>

using namespace llvm;
using namespace llvm::orc;

static std::unique_ptr<LLJIT> TheJIT;
static ResourceTrackerSP TheTracker;
static ResourceTrackerSP RuntimeTracker;

static std::unique_ptr<Module> create_runtime_module(LLVMContext &ctx) {
    auto M = std::make_unique<Module>("CantoRuntime", ctx);

    ArrayType *arr_type = ArrayType::get(Type::getInt64Ty(ctx), 65536);
    new GlobalVariable(
        *M, arr_type, false, GlobalValue::ExternalLinkage,
        ConstantAggregateZero::get(arr_type), "canto_repl_globals");

    return M;
}

extern "C" void jit_init(void) {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    auto jit = LLJITBuilder().create();
    if (!jit) {
        fprintf(stderr, "jit: failed to create LLJIT: %s\n",
                toString(jit.takeError()).c_str());
        return;
    }
    TheJIT = std::move(*jit);

    auto &dl = TheJIT->getDataLayout();
    auto  gen = DynamicLibrarySearchGenerator::GetForCurrentProcess(
        dl.getGlobalPrefix());
    if (!gen) {
        fprintf(stderr, "jit: failed to load process symbols: %s\n",
                toString(gen.takeError()).c_str());
        return;
    }
    TheJIT->getMainJITDylib().addGenerator(std::move(*gen));

    auto rt_ctx = std::make_unique<LLVMContext>();
    auto rt_mod = create_runtime_module(*rt_ctx);
    auto rt_tsm = ThreadSafeModule(std::move(rt_mod), std::move(rt_ctx));
    auto &JD = TheJIT->getMainJITDylib();
    RuntimeTracker = JD.createResourceTracker();
    if (auto err = TheJIT->addIRModule(RuntimeTracker, std::move(rt_tsm))) {
        fprintf(stderr, "jit: failed to add runtime module: %s\n",
                toString(std::move(err)).c_str());
    }
}

extern "C" int jit_run(void) {
    if (!TheJIT) {
        fprintf(stderr, "jit: not initialized\n");
        return -1;
    }
    if (!TheModule) {
        fprintf(stderr, "jit: no module to execute\n");
        return -1;
    }

    if (TheTracker) {
        if (auto err = TheTracker->remove()) {
            fprintf(stderr, "jit: failed to remove previous module: %s\n",
                    toString(std::move(err)).c_str());
        }
        TheTracker.reset();
    }

    std::string err_str;
    raw_string_ostream err_os(err_str);
    if (verifyModule(*TheModule, &err_os)) {
        fprintf(stderr, "jit: IR verification failed:\n%s\n",
                err_str.c_str());
        return -1;
    }

    TheModule->setDataLayout(TheJIT->getDataLayout());

    auto tsm = ThreadSafeModule(
        std::move(TheModule),
        std::move(TheContext));

    TheModule  = nullptr;
    TheContext = nullptr;
    Builder.reset();

    auto &JD = TheJIT->getMainJITDylib();
    TheTracker = JD.createResourceTracker();
    if (auto err = TheJIT->addIRModule(TheTracker, std::move(tsm))) {
        fprintf(stderr, "jit: addIRModule failed: %s\n",
                toString(std::move(err)).c_str());
        return -1;
    }

    auto sym = TheJIT->lookup("main");
    if (!sym) {
        fprintf(stderr, "jit: lookup of 'main' failed: %s\n",
                toString(sym.takeError()).c_str());
        return -1;
    }

    auto fn = sym->toPtr<int()>();
    int  result = fn();
    return result;
}

extern "C" void jit_free(void) {
    RuntimeTracker.reset();
    TheTracker.reset();
    TheJIT.reset();
}
