#include "canto/symtable.h"
#include "context.hpp"

Value* expr_gen(Node* node) {
	if (!node) return nullptr;

	switch(node->kind) {
		case NODE_IDENT: {
			if (IsRepl && TheReplGlobals)
				return repl_load(node->ident.sym);

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

		case NODE_ARRAY: {
			if (node->array.count == 0) {
				fprintf(stderr, "codegen: empty array literal not supported\n");
				return nullptr;
			}

			// Evaluate first element to determine element type
			Value *first = expr_gen(node->array.exprs[0]);
			if (!first) return nullptr;
			Type *elem_ty = first->getType();

			// Allocate array on the stack
			Value *size_val = ConstantInt::get(Builder->getInt64Ty(), node->array.count);
			Function *fn = Builder->GetInsertBlock()->getParent();
			IRBuilder<> entry_builder(&fn->getEntryBlock(), fn->getEntryBlock().begin());
			AllocaInst *arr = entry_builder.CreateAlloca(elem_ty, size_val, "arr");

			// Store each element
			for (uint32_t i = 0; i < node->array.count; i++) {
				Value *elem = (i == 0) ? first : expr_gen(node->array.exprs[i]);
				if (!elem) return nullptr;

				// Coerce if types differ
				if (elem->getType() != elem_ty) {
					if (elem_ty->isDoubleTy() && elem->getType()->isIntegerTy())
						elem = Builder->CreateSIToFP(elem, elem_ty);
					else if (elem_ty->isIntegerTy() && elem->getType()->isDoubleTy())
						elem = Builder->CreateFPToSI(elem, elem_ty);
				}

				Value *idx = ConstantInt::get(Builder->getInt64Ty(), i);
				Value *ptr = Builder->CreateGEP(elem_ty, arr, idx, "arr.elem");
				Builder->CreateStore(elem, ptr);
			}

			return arr;
		}

		case NODE_INDEX: {
			// Evaluate base pointer
			Value *base = expr_gen(node->index.left);
			if (!base) return nullptr;

			Value *idx = expr_gen(node->index.index);
			if (!idx) return nullptr;

			// Look up element type from variable tracking
			Type *elem_ty = nullptr;
			if (node->index.left->kind == NODE_IDENT) {
				std::string vname = sym_name(node->index.left->ident.sym);
				auto it = VariableElementTypes.find(vname);
				if (it != VariableElementTypes.end())
					elem_ty = it->second;
			}

			if (!elem_ty) {
				if (base->getType()->isPointerTy()) {
					// Default to i64 if we have no type info
					elem_ty = Type::getInt64Ty(*TheContext);
				} else {
					fprintf(stderr, "codegen: index target is not a pointer\n");
					return nullptr;
				}
			}

			Value *ptr = Builder->CreateGEP(elem_ty, base, idx, "idx.ptr");
			return Builder->CreateLoad(elem_ty, ptr, "idx.val");
		}

		default:
			fprintf(stderr, "Codegen Error: Unhandled node kind %d\n", node->kind);
			return nullptr;
	}
}

