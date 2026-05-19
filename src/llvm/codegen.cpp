#include "canto/codegen.h"
#include "canto/ast.h"
#include "context.hpp"

using namespace llvm;

extern "C" void codegen_init(void) {
	TheContext = std::make_unique<LLVMContext>();
	TheModule = std::make_unique<Module>("Canto", *TheContext);
	Builder = std::make_unique<IRBuilder<>>(*TheContext);

	// main function for llvm
	// get take a 
	//		return type
	//		and args
	FunctionType* fn_type = FunctionType::get(Builder->getInt32Ty(), false);
	Function* main_fn = Function::Create(
			fn_type,
			Function::ExternalLinkage, // this function is visible outside
			"main",
			TheModule.get()
	);

	BasicBlock* entry_block = BasicBlock::Create(*TheContext, "entry", main_fn);
	Builder->SetInsertPoint(entry_block);	
} 

extern "C" int codegen_eval_expr(Node *node){
	Value* result = expr_gen(node);
	if (!result) return -1;

	Value* ret;
	if (result->getType()->isDoubleTy()) 
		ret = Builder->CreateFPToSI(result, Builder->getInt32Ty(), "fptosi");
	else if (result->getType()->isIntegerTy(1)) // bool: i1 -> i32
		ret = Builder->CreateZExt(result, Builder->getInt32Ty(), "zext");
	else 
		ret = Builder->CreateIntCast(result, Builder->getInt32Ty(), true);

	Builder->CreateRet(ret);
	return 0;
}

extern "C" void codegen_dump(void) {
	std::error_code EC;
	raw_fd_ostream out(
		"build/canto.ll",
		EC
	);

	if (EC) {
        errs() << "Error opening file: "
               << EC.message() << "\n";
        return;
    }

	TheModule->print(out, nullptr);
}

extern "C" void codegen_free() {
	Builder.reset();
	TheModule.reset();
	TheContext.reset();
	NamedValues.clear();
}

