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
			Function* fn = Builder->GetInsertBlock()->getParent();

			// `if cond | loop { body }` — while loop
			if (node->if_.is_loop) {
				BasicBlock* cond_bb = BasicBlock::Create(*TheContext, "loop.cond", fn);
				BasicBlock* body_bb = BasicBlock::Create(*TheContext, "loop.body", fn);
				BasicBlock* exit_bb = BasicBlock::Create(*TheContext, "loop.exit", fn);

				Builder->CreateBr(cond_bb);

				Builder->SetInsertPoint(cond_bb);
				Value* lc = expr_gen(node->if_.cond);
				if (!lc) return nullptr;
				if (!lc->getType()->isIntegerTy(1))
					lc = Builder->CreateICmpNE(lc,
							ConstantInt::get(lc->getType(), 0),
							"loopcond");
				Builder->CreateCondBr(lc, body_bb, exit_bb);

				LoopStack.push_back({cond_bb, exit_bb});
				Builder->SetInsertPoint(body_bb);
				stmt_gen(node->if_.then_);
				if (!Builder->GetInsertBlock()->getTerminator())
					Builder->CreateBr(cond_bb);
				LoopStack.pop_back();

				Builder->SetInsertPoint(exit_bb);
				return ConstantInt::get(Builder->getInt32Ty(), 0);
			}

			Value* cond = expr_gen(node->if_.cond);
			if (!cond) return NULL;

			if (!cond->getType()->isIntegerTy(1))
				cond = Builder->CreateICmpNE(cond,
							ConstantInt::get(cond->getType(), 0),
							"ifcond");

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

		case NODE_LOOP: {
			Function* fn = Builder->GetInsertBlock()->getParent();
			BasicBlock* cond_bb = BasicBlock::Create(*TheContext, "loop.cond", fn);
			BasicBlock* body_bb = BasicBlock::Create(*TheContext, "loop.body", fn);
			BasicBlock* exit_bb = BasicBlock::Create(*TheContext, "loop.exit", fn);

			AllocaInst* counter = nullptr;
			Value*      limit   = nullptr;

			if (node->loop.count) {
				limit = expr_gen(node->loop.count);
				if (!limit) return nullptr;
				IRBuilder<> entry(&fn->getEntryBlock(),
								  fn->getEntryBlock().begin());
				counter = entry.CreateAlloca(limit->getType(), nullptr, "loop.i");
				Builder->CreateStore(ConstantInt::get(limit->getType(), 0), counter);
			}

			// for counted loops, continue jumps to an inc block; otherwise to cond
			BasicBlock* cont_bb = cond_bb;
			BasicBlock* inc_bb  = nullptr;
			if (counter) {
				inc_bb = BasicBlock::Create(*TheContext, "loop.inc", fn);
				cont_bb = inc_bb;
			}

			Builder->CreateBr(cond_bb);

			Builder->SetInsertPoint(cond_bb);
			Value* cv = nullptr;
			if (node->loop.cond) {
				cv = expr_gen(node->loop.cond);
				if (!cv) return nullptr;
				if (!cv->getType()->isIntegerTy(1))
					cv = Builder->CreateICmpNE(cv,
							ConstantInt::get(cv->getType(), 0), "loopcond");
			} else if (counter) {
				Value* cur = Builder->CreateLoad(limit->getType(), counter, "loop.cur");
				cv = Builder->CreateICmpSLT(cur, limit, "loop.lt");
			} else {
				cv = ConstantInt::getTrue(*TheContext);
			}
			Builder->CreateCondBr(cv, body_bb, exit_bb);

			LoopStack.push_back({cont_bb, exit_bb});
			Builder->SetInsertPoint(body_bb);
			stmt_gen(node->loop.body);
			if (!Builder->GetInsertBlock()->getTerminator())
				Builder->CreateBr(cont_bb);
			LoopStack.pop_back();

			if (inc_bb) {
				Builder->SetInsertPoint(inc_bb);
				Value* cur = Builder->CreateLoad(limit->getType(), counter, "loop.cur");
				Value* nxt = Builder->CreateAdd(cur,
						ConstantInt::get(limit->getType(), 1), "loop.inc");
				Builder->CreateStore(nxt, counter);
				Builder->CreateBr(cond_bb);
			}

			Builder->SetInsertPoint(exit_bb);
			return ConstantInt::get(Builder->getInt32Ty(), 0);
		}

		case NODE_CONTINUE: {
			if (LoopStack.empty()) {
				fprintf(stderr, "codegen: 'continue' outside of loop\n");
				return nullptr;
			}
			Builder->CreateBr(LoopStack.back().first);
			Function* fn = Builder->GetInsertBlock()->getParent();
			BasicBlock* dead = BasicBlock::Create(*TheContext, "after.continue", fn);
			Builder->SetInsertPoint(dead);
			return ConstantInt::get(Builder->getInt32Ty(), 0);
		}

		case NODE_BREAK: {
			if (LoopStack.empty()) {
				fprintf(stderr, "codegen: 'break' outside of loop\n");
				return nullptr;
			}
			Builder->CreateBr(LoopStack.back().second);
			Function* fn = Builder->GetInsertBlock()->getParent();
			BasicBlock* dead = BasicBlock::Create(*TheContext, "after.break", fn);
			Builder->SetInsertPoint(dead);
			return ConstantInt::get(Builder->getInt32Ty(), 0);
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

