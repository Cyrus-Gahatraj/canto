#include "context.hpp"

Value* stmt_gen(Node* node) {
	if (!node) return nullptr;

	switch (node->kind) {

		case NODE_LET: {
			Value* val = expr_gen(node->let.value);
			if(!val) return nullptr;

			// Get the current function, find it main entry block
			Function* fn = Builder->GetInsertBlock()->getParent();
			IRBuilder<> entry(&fn->getEntryBlock(),
							fn->getEntryBlock().begin());

			std::string var_name = "var_" + std::to_string(node->let.name_sym);
			AllocaInst* slot = entry.CreateAlloca(
				val->getType(),
				nullptr,
				var_name);
			

			Builder->CreateStore(val, slot);
			NamedValues[var_name] = slot;

			return val;
	    }

		case NODE_WRITE: {
			 Function *printf_fn = TheModule->getFunction("printf");
			 if (!printf_fn) {
				 PointerType* point_to = PointerType::get(Builder->getInt8Ty(), 0);
				 FunctionType *printf_type = FunctionType::get(
												 Builder->getInt32Ty(),
												 point_to, true);
				 printf_fn = Function::Create(
						 printf_type,
						 Function::ExternalLinkage,
						 "printf",
						 TheModule.get());
			 }

			 for (uint32_t i = 0; i < node->write.count; i++) {
				 Node  *arg = node->write.exprs[i];
				 Value *val = expr_gen(arg);
				 if (!val) continue;

				 Type *ty = val->getType();

				 if (ty->isPointerTy()) {
					 Value *fmt = Builder->CreateGlobalStringPtr("%s", "fmt_str");
					 Builder->CreateCall(printf_fn, { fmt, val });
				 }
				 else if (arg->kind == NODE_STRING_LIT) {
					 Value *fmt = Builder->CreateGlobalStringPtr("%s", "fmt_str");
					 Builder->CreateCall(printf_fn, { fmt, val });
				 }
				 else if (ty->isDoubleTy()) {
					 Value *fmt = Builder->CreateGlobalStringPtr("%f", "fmt_f64");
					 Builder->CreateCall(printf_fn, { fmt, val });
				 }
				 else if (ty->isIntegerTy(1)) {
					 Value *fmt  = Builder->CreateGlobalStringPtr("%s",    "fmt_bool");
					 Value *tstr = Builder->CreateGlobalStringPtr("true",  "true_str");
					 Value *fstr = Builder->CreateGlobalStringPtr("false", "false_str");
					 Value *sel  = Builder->CreateSelect(val, tstr, fstr, "bool_str");
					 Builder->CreateCall(printf_fn, { fmt, sel });
				 }
				 else if (ty->isIntegerTy(64)) {
					 Value *fmt = Builder->CreateGlobalStringPtr("%lld", "fmt_i64");
					 Builder->CreateCall(printf_fn, { fmt, val });
				 }
				 else if (ty->isIntegerTy()) {
					 Value *fmt  = Builder->CreateGlobalStringPtr("%d", "fmt_int");
					 Value *cast = Builder->CreateIntCast(
							 val, Builder->getInt32Ty(), true);
					 Builder->CreateCall(printf_fn, { fmt, cast });
				 }
				 else {
					 fprintf(stderr, "codegen: write: unhandled type for arg %u\n", i);
				 }
			 }

			 Value *newline = Builder->CreateGlobalStringPtr("\n", "newline");
			 Builder->CreateCall(printf_fn, { newline });
			 return ConstantInt::get(Builder->getInt32Ty(), 0);
		}
		
		case NODE_IF: {
			Value* cond = expr_gen(node->if_.cond);
			if (!cond) return NULL;

			if (!cond->getType()->isIntegerTy(1))
				cond = Builder->CreateICmpNE(cond,
							ConstantInt::get(cond->getType(), 0),
							"ifcond");

			Function* fn = Builder->GetInsertBlock()->getParent();
			BasicBlock* then_bb = BasicBlock::Create(*TheContext, "then", fn);
			BasicBlock* else_bb = BasicBlock::Create(*TheContext, "else", fn);
			BasicBlock* merge_bb = BasicBlock::Create(*TheContext, "merge", fn);

			Builder->CreateCondBr(cond, then_bb,
					node->if_.else_ ? else_bb : merge_bb);

			// then
			Builder->SetInsertPoint(then_bb);
			stmt_gen(node->if_.then_);
			if (!Builder->GetInsertBlock()->getTerminator())
				Builder->CreateBr(merge_bb);

			// else
			if (node->if_.else_) {
				Builder->SetInsertPoint(else_bb);
				stmt_gen(node->if_.else_);
				if (!Builder->GetInsertBlock()->getTerminator())
					Builder->CreateBr(merge_bb);
			} else {
				Builder->SetInsertPoint(else_bb);
				Builder->CreateBr(merge_bb);
			}

			// merge
			Builder->SetInsertPoint(merge_bb);
			return ConstantInt::get(Builder->getInt32Ty(), 0);
		}

		case NODE_EDIT: {
			Node *target = node->edit.target;

			// resolve the target alloca 
			if (target->kind != NODE_IDENT) {
				fprintf(stderr, "codegen: edit target must be identifier (design fields coming later)\n");
				return nullptr;
			}

			std::string name = "var_" + std::to_string(target->ident.sym);
			auto it = NamedValues.find(name);
			if (it == NamedValues.end()) {
				fprintf(stderr, "codegen: undefined variable '%s' in edit\n",
						name.c_str());
				return nullptr;
			}
			AllocaInst *slot = it->second;

			// process each pair 
			for (uint32_t i = 0; i < node->edit.pair_count; i++) {
				Node *pair = node->edit.pairs[i];

				if (pair->edit_pair.value->kind == NODE_RELATIVE) {
					Node     *rel     = pair->edit_pair.value;
					Value    *current = Builder->CreateLoad(
						slot->getAllocatedType(), slot, "edit_cur");
					Value    *operand = expr_gen(rel->relative.expr);
					if (!operand) return nullptr;

					Value *result = nullptr;
					bool   fp     = current->getType()->isDoubleTy() ||
									operand->getType()->isDoubleTy();

					if (fp) {
						if (current->getType()->isIntegerTy())
							current = Builder->CreateSIToFP(
								current, Type::getDoubleTy(*TheContext));
						if (operand->getType()->isIntegerTy())
							operand = Builder->CreateSIToFP(
								operand, Type::getDoubleTy(*TheContext));
					}

					switch (rel->relative.op) {
						case TK_PLUS:
							result = fp ? Builder->CreateFAdd(current, operand, "edit_add")
										: Builder->CreateAdd (current, operand, "edit_add");
							break;
						case TK_MINUS:
							result = fp ? Builder->CreateFSub(current, operand, "edit_sub")
										: Builder->CreateSub (current, operand, "edit_sub");
							break;
						case TK_STAR:
							result = fp ? Builder->CreateFMul(current, operand, "edit_mul")
										: Builder->CreateMul (current, operand, "edit_mul");
							break;
						case TK_SLASH:
							result = fp ? Builder->CreateFDiv(current, operand, "edit_div")
										: Builder->CreateSDiv(current, operand, "edit_div");
							break;
						default:
							fprintf(stderr, "codegen: unknown relative op\n");
							return nullptr;
					}

					Builder->CreateStore(result, slot);

				} else {
					// absolute set — evaluate and store 
					Value *val = expr_gen(pair->edit_pair.value);
					if (!val) return nullptr;

					// cast if types differ 
					if (val->getType() != slot->getAllocatedType()) {
						if (slot->getAllocatedType()->isDoubleTy() &&
							val->getType()->isIntegerTy())
							val = Builder->CreateSIToFP(
								val, Type::getDoubleTy(*TheContext));
						else if (slot->getAllocatedType()->isIntegerTy() &&
								 val->getType()->isDoubleTy())
							val = Builder->CreateFPToSI(
								val, slot->getAllocatedType());
					}

					Builder->CreateStore(val, slot);
				}
			}

			// edit evaluates to the final value of target
			return Builder->CreateLoad(slot->getAllocatedType(), slot, "edit_result");
		}

		case NODE_PROGRAM:
        case NODE_BLOCK: {
            Value *last = nullptr;
            for (uint32_t i = 0; i < node->block.count; i++)
                last = stmt_gen(node->block.stmts[i]);
            return last;
        }

		default:
			return expr_gen(node);
	}
}

