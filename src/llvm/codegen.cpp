#include "canto/codegen.h"
#include "canto/ast.h"
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
			"main",
			TheModule.get()
	);

	BasicBlock* entry_block = BasicBlock::Create(*TheContext, "entry", main_fn);
	Builder->SetInsertPoint(entry_block);	
} 

static Value* code_gen(Node* node) {
	if (!node) return nullptr;

	switch(node->kind) {
		case NODE_INT_LIT: {
		   return ConstantInt::get(
				   Type::getInt64Ty(*TheContext),
				   node->int_lit.value);
	    }

		case NODE_DOUBLE_LIT:
            return ConstantFP::get(Type::getDoubleTy(*TheContext),
                                   node->double_lit.value);

		case NODE_BOOL_LIT:
			// 1 or 0 depending on true or false
			return ConstantInt::get(Type::getInt1Ty(*TheContext),
									node->bool_lit.value? 1 : 0);

		case NODE_UNARY: {
            Value *operand = code_gen(node->unary.expr);
            if (!operand) return nullptr;

            switch (node->unary.op) {
                case TK_MINUS:
                    if (operand->getType()->isDoubleTy())
                        return Builder->CreateFNeg(operand, "fneg");
                    return Builder->CreateNeg(operand, "neg");

                case TK_BANG:
                    return Builder->CreateNot(operand, "not");

                default:
                    fprintf(stderr, "codegen: unknown unary op\n");
                    return nullptr;
            }
        }

		case NODE_BINARY: {
			Value* L = code_gen(node->binary.left);
			Value* R = code_gen(node->binary.right);

			if (!L || !R) return nullptr;

			bool is_float = L->getType()->isDoubleTy() ||
							R->getType()->isDoubleTy();

			if (is_float) {
				if (L->getType()->isIntegerTy())
					L = Builder->CreateSIToFP(L, Type::getDoubleTy(*TheContext));
				if (R->getType()->isIntegerTy())
					R = Builder->CreateSIToFP(R, Type::getDoubleTy(*TheContext));
			}

			switch (node->binary.op) {

				// Arithematic
				case TK_PLUS:
					return is_float ? Builder->CreateFAdd(L, R, "fadd")
									: Builder->CreateAdd(L, R, "add");

				case TK_MINUS:
                    return is_float ? Builder->CreateFSub(L, R, "fsub")
                                    : Builder->CreateSub (L, R, "sub");
                case TK_STAR:
                    return is_float ? Builder->CreateFMul(L, R, "fmul")
                                    : Builder->CreateMul (L, R, "mul");
                case TK_SLASH:
                    return is_float ? Builder->CreateFDiv(L, R, "fdiv")
                                    : Builder->CreateSDiv(L, R, "sdiv");
                case TK_PERCENTAGE:
                    return is_float ? Builder->CreateFRem(L, R, "frem")
                                    : Builder->CreateSRem(L, R, "srem");

				// comparision
				case TK_LT:
                    return is_float ? Builder->CreateFCmpOLT(L, R, "flt")
                                    : Builder->CreateICmpSLT(L, R, "ilt");
                case TK_GT:
                    return is_float ? Builder->CreateFCmpOGT(L, R, "fgt")
                                    : Builder->CreateICmpSGT(L, R, "igt");
                case TK_LEQ:
                    return is_float ? Builder->CreateFCmpOLE(L, R, "fle")
                                    : Builder->CreateICmpSLE(L, R, "ile");
                case TK_GEQ:
                    return is_float ? Builder->CreateFCmpOGE(L, R, "fge")
                                    : Builder->CreateICmpSGE(L, R, "ige");
                case TK_EQUAL:
                    return is_float ? Builder->CreateFCmpOEQ(L, R, "feq")
                                    : Builder->CreateICmpEQ (L, R, "ieq");
                case TK_NOT_EQUAL:
                    return is_float ? Builder->CreateFCmpONE(L, R, "fne")
                                    : Builder->CreateICmpNE (L, R, "ine");

				// Logical
				case TK_BOOL_AND:
                    return Builder->CreateAnd(L, R, "and");
                case TK_BOOL_OR:
                    return Builder->CreateOr (L, R, "or");

				default:
					fprintf(stderr, "codegen: unknown binary operation\n");
					return nullptr;
			}
		}
		case NODE_GROUP:
            return code_gen(node->group.expr);

		default:
			return nullptr;
	}
}

extern "C" int codegen_eval_expr(Node *node){
	Value* result = code_gen(node);
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
	TheModule.reset();
	TheContext.reset();
	Builder.reset();
}

