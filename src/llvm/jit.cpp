// jit.cpp — JIT (Just-In-Time) compilation and execution
//
// JIT compilation means: instead of writing the compiled program to a file,
// we compile it directly into memory and run it immediately.
//
// This file uses LLVM's ORC JIT framework (Optimized Runtime Compilation).
// The key steps are:
//   1. Initialize the host machine's code generation backend
//   2. Create an LLJIT instance (the JIT engine)
//   3. Add our LLVM module (the compiled program) to the JIT
//   4. Look up the `main` symbol and call it
//
// The JIT also makes all symbols from the current process available to
// generated code, so `printf`, `malloc`, etc. all work out of the box.

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

// The JIT engine — keeps the compiled code in memory until jit_free() is called
std::unique_ptr<LLJIT> TheJIT;

// Resource tracker — used to unload previously loaded modules when re-running
static ResourceTrackerSP TheTracker;

// ---------------------------------------------------------------------------
// jit_init — set up the JIT engine (call once at startup)
// ---------------------------------------------------------------------------

extern "C" void jit_init(void) {
    // Initialize LLVM's code generators for the host CPU
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    // Create the LLJIT engine
    auto jit = LLJITBuilder().create();
    if (!jit) {
        fprintf(stderr, "JIT Error: Failed to create LLJIT engine: %s\n",
                toString(jit.takeError()).c_str());
        return;
    }
    TheJIT = std::move(*jit);

    // Allow generated code to call any symbol available in the current process
    // (e.g. printf, malloc, custom runtime functions)
    auto &dl  = TheJIT->getDataLayout();
    auto  gen = DynamicLibrarySearchGenerator::GetForCurrentProcess(dl.getGlobalPrefix());
    if (!gen) {
        fprintf(stderr, "JIT Error: Failed to load process symbols: %s\n",
                toString(gen.takeError()).c_str());
        return;
    }
    TheJIT->getMainJITDylib().addGenerator(std::move(*gen));
}

// ---------------------------------------------------------------------------
// jit_run — compile the current module and execute `main`
// ---------------------------------------------------------------------------

// Returns the exit code returned by `main`, or -1 on error.
extern "C" int jit_run(void) {
    if (!TheJIT) {
        fprintf(stderr, "JIT Error: JIT engine not initialized\n");
        return -1;
    }
    if (!TheModule) {
        fprintf(stderr, "JIT Error: No LLVM module to execute\n");
        return -1;
    }

    // Remove the previous module from the JIT (for REPL re-evaluation)
    if (TheTracker) {
        cantFail(TheTracker->remove());
        TheTracker.reset();
    }

    // Verify the module is well-formed before trying to compile it
    std::string err_str;
    raw_string_ostream err_os(err_str);
    if (verifyModule(*TheModule, &err_os)) {
        fprintf(stderr, "JIT Error: LLVM IR verification failed:\n%s\n", err_str.c_str());
        return -1;
    }

    // Set the data layout so the JIT knows the target's memory model
    TheModule->setDataLayout(TheJIT->getDataLayout());

    // Wrap the module and context in a ThreadSafeModule for thread-safe JIT use.
    // After this, TheModule and TheContext are moved and become null.
    auto tsm = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
    TheModule  = nullptr;
    TheContext = nullptr;
    Builder.reset();

    // Add the module to the JIT with a resource tracker (for later removal)
    auto &JD  = TheJIT->getMainJITDylib();
    TheTracker = JD.createResourceTracker();

    if (auto err = TheJIT->addIRModule(TheTracker, std::move(tsm))) {
        fprintf(stderr, "JIT Error: Failed to add module: %s\n",
                toString(std::move(err)).c_str());
        return -1;
    }

    // Look up the `main` function symbol in the JIT
    auto sym = TheJIT->lookup("main");
    if (!sym) {
        fprintf(stderr, "JIT Error: Could not find 'main': %s\n",
                toString(sym.takeError()).c_str());
        return -1;
    }

    // Call main() and return its exit code
    auto fn = sym->toPtr<int()>();
    return fn();
}

// ---------------------------------------------------------------------------
// jit_free — tear down the JIT engine
// ---------------------------------------------------------------------------

extern "C" void jit_free(void) {
    TheTracker.reset();
    TheJIT.reset();
}
