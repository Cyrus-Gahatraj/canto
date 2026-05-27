#include "canto/jit.h"
#include "context.hpp"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Error.h"
#include "llvm/IR/Verifier.h"
#include <cstdio>
#include <memory>

using namespace llvm;
using namespace llvm::orc;

std::unique_ptr<LLJIT> TheJIT;
static ResourceTrackerSP TheTracker;

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
        cantFail(TheTracker->remove());
        TheTracker.reset();
    }

    std::string err_str;
    raw_string_ostream err_os(err_str);

    if (verifyModule(*TheModule, &err_os)) {
        fprintf(stderr, "%s\n", err_str.c_str());
        return -1;
    }

    TheModule->setDataLayout(TheJIT->getDataLayout());

    auto tsm = ThreadSafeModule(
        std::move(TheModule),
        std::move(TheContext)
    );

    TheModule = nullptr;
    TheContext = nullptr;
    Builder.reset();

    auto &JD = TheJIT->getMainJITDylib();

    TheTracker = JD.createResourceTracker();

    if (auto err = TheJIT->addIRModule(
            TheTracker,
            std::move(tsm))) {
        fprintf(stderr, "%s\n",
                toString(std::move(err)).c_str());
        return -1;
    }

    auto sym = TheJIT->lookup("main");

    if (!sym) {
        fprintf(stderr, "%s\n",
                toString(sym.takeError()).c_str());
        return -1;
    }

    auto fn = sym->toPtr<int()>();
    return fn();
}

extern "C" void jit_free(void) {
    TheTracker.reset();
    TheJIT.reset();
}
