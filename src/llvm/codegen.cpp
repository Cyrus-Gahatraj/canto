#include "canto/codegen.h"
#include "canto/ast.h"
#include "context.hpp"
#include <format>

using namespace llvm;

extern "C" void codegen_init(void) {
	NamedValues.clear();
	TheContext = std::make_unique<LLVMContext>();
	TheModule = std::make_unique<Module>("Canto", *TheContext);
	Builder = std::make_unique<IRBuilder<>>(*TheContext);

	// main function for llvm
	// get take a 
	//		return type
	//		and args
	//		define i32 @main() { entry: }
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
	Value* result = stmt_gen(node);
	if (!result) return -1;
	return 0;
}

extern "C" void codegen_dump(const char* output_path) {
	std::error_code EC;
	raw_fd_ostream out(output_path, EC);

	if (EC) {
        errs() << "Error opening file: "
               << EC.message() << "\n";
        return;
    }

	TheModule->print(out, nullptr);
}

extern "C" void codegen_print_ir(void) {
    TheModule->print(errs(), nullptr);
}

extern "C" void codegen_free() {
	Builder.reset();
	TheModule.reset();
	TheContext.reset();
	NamedValues.clear();
	TheSymtable = nullptr;
}

extern "C" void codegen_set_symtable(SymTable *table) {
    TheSymtable = table;
}

extern "C" void codegen_finalize(int ret) {
	Builder->CreateRet(Builder->getInt32(ret));
}

