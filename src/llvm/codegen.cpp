#include "canto/codegen.h"
#include "llvm/IR/Verifier.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>
#include <map>

using namespace llvm;

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder;
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;

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
			"test_entry",
			TheModule.get()
	);

	BasicBlock* entry_block = BasicBlock::Create(*TheContext, "entry", main_fn);
	Builder->SetInsertPoint(entry_block);	
} 

static Value* code_gen(Node* node) {
	if (!node) return nullptr;

	switch(node->kind) {
		case NODE_INT_LIT: {
		   auto value = node->int_lit.value;	
		   return ConstantInt::get(
				   Type::getInt64Ty(*TheContext),
				   value
				  );
	    }
		default:
			return nullptr;
	}
}

extern "C" int codegen_eval_expr(Node *node){
	Value* result = code_gen(node);
	if (!result) return -1;

	auto i64Result = 
		Builder->CreateIntCast(result, Builder->getInt64Ty(), true);

	Builder->CreateRet(i64Result);
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

