#include "canto/codegen.h"
#include "canto/ast.h"
#include "canto/keyword_modifier.h"
#include "context.hpp"
#include <format>

using namespace llvm;

std::map<uint32_t, KeywordInstance*> KeywordModifiers;
Value *LastExprResult = nullptr;
GlobalVariable *TheReplGlobals = nullptr;
bool IsRepl = false;

extern "C" void codegen_init(void) {
	NamedValues.clear();
	LoopStack.clear();
	KeywordModifiers.clear();
	LastExprResult = nullptr;
	TheReplGlobals = nullptr;
	TheContext = std::make_unique<LLVMContext>();
	TheModule = std::make_unique<Module>("Canto", *TheContext);
	Builder = std::make_unique<IRBuilder<>>(*TheContext);

	if (IsRepl) {
		ArrayType *arr_type = ArrayType::get(Type::getInt64Ty(*TheContext), 65536);
		TheReplGlobals = new GlobalVariable(
			*TheModule, arr_type, false, GlobalValue::ExternalLinkage,
			nullptr, "canto_repl_globals");
		TheReplGlobals->setExternallyInitialized(true);
	}

	FunctionType* fn_type = FunctionType::get(Builder->getInt32Ty(), false);
	Function* main_fn = Function::Create(
			fn_type,
			Function::ExternalLinkage,
			"main",
			TheModule.get()
	);

	BasicBlock* entry_block = BasicBlock::Create(*TheContext, "entry", main_fn);
	Builder->SetInsertPoint(entry_block);	
} 

extern "C" int codegen_eval_expr(Node *node){
	Value* result = stmt_gen(node);
	LastExprResult = result;
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
	LoopStack.clear();
	LastExprResult = nullptr;
	TheReplGlobals = nullptr;
	for (auto &kv : KeywordModifiers)
		free_keyword_instance(kv.second);
	KeywordModifiers.clear();
	TheSymtable = nullptr;
}

extern "C" void codegen_set_symtable(SymTable *table) {
    TheSymtable = table;
}

extern "C" void codegen_set_repl_mode(bool repl) {
	IsRepl = repl;
}

extern "C" void codegen_finalize(int ret) {
	Builder->CreateRet(Builder->getInt32(ret));
}

extern "C" void codegen_finalize_repl(void) {
	if (LastExprResult) {
		Value *val = LastExprResult;
		Type *ty = val->getType();
		if (ty->isDoubleTy())
			val = Builder->CreateFPToSI(val, Builder->getInt32Ty());
		else if (ty->isIntegerTy(1))
			val = Builder->CreateIntCast(val, Builder->getInt32Ty(), true);
		else if (ty->isIntegerTy())
			val = Builder->CreateIntCast(val, Builder->getInt32Ty(), true);
		else if (ty->isPointerTy())
			val = Builder->getInt32(0);
		Builder->CreateRet(val);
	} else {
		Builder->CreateRet(Builder->getInt32(0));
	}
}
