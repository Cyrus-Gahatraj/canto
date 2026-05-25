#include "canto/symtable.h"
#include "context.hpp"

Value* expr_gen(Node* node) {
	if (!node) return nullptr;

	switch(node->kind) {
		case NODE_IDENT: {
			std::string key = sym_name(node->ident.sym);

			auto it = NamedValues.find(key);
			if (it == NamedValues.end()) {
				fprintf(stderr, "Compiler Error: Undefined variable sym=%u\n",
							node->ident.sym);
				return nullptr;
			}

			AllocaInst* slot = it->second;
			return Builder->CreateLoad(slot->getAllocatedType(), slot, "load");
		}

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

		case NODE_STRING_LIT: {
			if (!TheSymtable) {
				fprintf(stderr, "codegen: no symbol table set\n");
				return nullptr;
			}

			const Symbol* sym = &TheSymtable->syms[node->string_lit.sym];

			return Builder->CreateGlobalStringPtr(
					StringRef(sym->start, sym->length), "str");

		}

		case NODE_UNARY: {
            Value *operand = expr_gen(node->unary.expr);
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
			Value* L = expr_gen(node->binary.left);
			Value* R = expr_gen(node->binary.right);

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
				case TK_KW_AND:
                    return Builder->CreateAnd(L, R, "and");
                case TK_KW_OR:
                    return Builder->CreateOr (L, R, "or");

				default:
					fprintf(stderr, "codegen: unknown binary operation\n");
					return nullptr;
			}
		}
		case NODE_GROUP:
            return expr_gen(node->group.expr);

		case NODE_KEYWORD:
			return ConstantInt::get(Builder->getInt32Ty(), 0);

		case NODE_CALL:
		case NODE_EDIT:
			return stmt_gen(node);

		default:
			fprintf(stderr, "Codegen Error: Unhandled node kind %d\n", node->kind);
			return nullptr;
	}
}

