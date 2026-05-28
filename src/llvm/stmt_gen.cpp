#include "context.hpp"

Value* stmt_gen(Node* node) {
	if (!node) return nullptr;

	switch (node->kind) {

		case NODE_LET: {
			if (node->let.value && node->let.value->kind == NODE_EDIT) {
				Node *target = node->let.value->edit.target;
				KeywordMeta *meta = nullptr;
				uint32_t tk_type = 0;
				if (target->kind == NODE_KEYWORD) {
					tk_type = target->keyword.tk_type;
					meta = get_keyword_meta(tk_type);
				} else if (target->kind == NODE_IDENT) {
					std::string tname = sym_name(target->ident.sym);
					meta = get_keyword_meta_by_name(tname.c_str());
					if (meta) tk_type = meta->tk_type;
				}

				if (meta) {
					Node *edit_node = node->let.value;
					KeywordInstance *inst = create_keyword_instance(tk_type);
					for (uint32_t i = 0; i < edit_node->edit.pair_count; i++) {
						Node *pair = edit_node->edit.pairs[i];
						std::string attr_name;
						if (pair->edit_pair.field_sym != 0) {
							const Symbol *s = &TheSymtable->syms[pair->edit_pair.field_sym];
							attr_name = std::string(s->start, s->length);
						} else if (meta->attr_count > 0) {
							attr_name = meta->attributes[0].name;
						}
						if (attr_name.empty()) continue;
						if (pair->edit_pair.value->kind == NODE_STRING_LIT) {
							const Symbol *s = &TheSymtable->syms[pair->edit_pair.value->string_lit.sym];
							std::string val(s->start, s->length);
							apply_keyword_edit(inst, attr_name.c_str(), val.c_str());
						} else if (pair->edit_pair.value->kind == NODE_IDENT) {
							const Symbol *s = &TheSymtable->syms[pair->edit_pair.value->ident.sym];
							std::string val(s->start, s->length);
							apply_keyword_edit(inst, attr_name.c_str(), val.c_str());
						} else if (pair->edit_pair.value->kind == NODE_BOOL_LIT) {
							std::string val = pair->edit_pair.value->bool_lit.value ? "true" : "false";
							apply_keyword_edit(inst, attr_name.c_str(), val.c_str());
						}
					}
					KeywordModifiers[node->let.name_sym] = inst;
					return ConstantInt::get(Builder->getInt32Ty(), 0);
				}
			}

			Value* val = expr_gen(node->let.value);
			if(!val) return nullptr;

			std::string var_name = sym_name(node->let.name_sym);

			// Record array element types for variable
			if (node->let.value && node->let.value->kind == NODE_ARRAY && node->let.value->array.count > 0) {
				Value* first_elem = expr_gen(node->let.value->array.exprs[0]);
				if (first_elem) {
					VariableElementTypes[var_name] = first_elem->getType();
				}
			}

			// Handle custom type annotation if provided
			Type* custom_type = nullptr;
			bool is_many = false;
			if (node->let.type_ann && node->let.type_ann->kind == NODE_IDENT) {
				auto it = KeywordModifiers.find(node->let.type_ann->ident.sym);
				if (it != KeywordModifiers.end()) {
					const char* name_val = get_keyword_attr(it->second, "name");
					const char* is_many_val = get_keyword_attr(it->second, "is_many");
					if (is_many_val && strcmp(is_many_val, "true") == 0) {
						is_many = true;
					}
					if (name_val) {
						if (strcmp(name_val, "integer") == 0 || strcmp(name_val, "int") == 0) {
							custom_type = Type::getInt64Ty(*TheContext);
						} else if (strcmp(name_val, "double") == 0 || strcmp(name_val, "float") == 0) {
							custom_type = Type::getDoubleTy(*TheContext);
						} else if (strcmp(name_val, "bool") == 0 || strcmp(name_val, "boolean") == 0) {
							custom_type = Type::getInt1Ty(*TheContext);
						} else if (strcmp(name_val, "string") == 0) {
							custom_type = PointerType::get(Builder->getInt8Ty(), 0);
						}
					}
				}
			}

			if (custom_type) {
				if (is_many) {
					VariableElementTypes[var_name] = custom_type;
					custom_type = PointerType::get(custom_type, 0);
				}
				if (val->getType() != custom_type) {
					if (custom_type->isDoubleTy() && val->getType()->isIntegerTy()) {
						val = Builder->CreateSIToFP(val, custom_type);
					} else if (custom_type->isIntegerTy() && val->getType()->isDoubleTy()) {
						val = Builder->CreateFPToSI(val, custom_type);
					} else if (custom_type->isIntegerTy() && val->getType()->isIntegerTy()) {
						val = Builder->CreateIntCast(val, custom_type, true);
					}
				}
			}

			if (IsRepl && TheReplGlobals) {
				return repl_store(node->let.name_sym, val);
			}

			Function* fn = Builder->GetInsertBlock()->getParent();
			IRBuilder<> entry(&fn->getEntryBlock(),
							fn->getEntryBlock().begin());

			AllocaInst* slot = entry.CreateAlloca(
				custom_type ? custom_type : val->getType(),
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

			 const char *end_str = "\n";
			 if (node->write.modifier_sym != 0) {
				 auto it = KeywordModifiers.find(node->write.modifier_sym);
				 if (it != KeywordModifiers.end()) {
					 const char *val = get_keyword_attr(it->second, "end");
					 if (val) end_str = val;
				 } else {
					 std::string kw_name = sym_name(node->write.modifier_sym);
					 KeywordMeta *meta = get_keyword_meta_by_name(kw_name.c_str());
					 if (meta) {
						 for (uint32_t a = 0; a < meta->attr_count; a++) {
							 if (strcmp(meta->attributes[a].name, "end") == 0) {
								 end_str = meta->attributes[a].default_value;
								 break;
							 }
						 }
					 }
				 }
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

			 Value *newline = Builder->CreateGlobalStringPtr(end_str, "write_end");
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

		case NODE_FN: {
			std::string name = sym_name(node->fn.name_sym);

			// build parameter types 
			std::vector<Type*> param_types;
			for (uint32_t i = 0; i < node->fn.param_count; i++) {
				// default to i64 — type inference comes later 
				param_types.push_back(Type::getInt64Ty(*TheContext));
			}

			// return type — default i64 
			Type *ret_type = Type::getInt64Ty(*TheContext);

			FunctionType* ft = FunctionType::get(ret_type, param_types, false);
			Function* fn = Function::Create(
				ft, Function::ExternalLinkage, name, TheModule.get());

			// name the parameters 
			uint32_t idx = 0;
			for (auto &arg : fn->args()) {
				arg.setName(sym_name(node->fn.params[idx]->param.name_sym));
				idx++;
			}

			// save and switch insert point
			BasicBlock *saved_bb = Builder->GetInsertBlock();

			BasicBlock *entry = BasicBlock::Create(*TheContext, "entry", fn);
			Builder->SetInsertPoint(entry);

			// allocate each parameter on the stack 
			idx = 0;
			for (auto &arg : fn->args()) {
				std::string pname = std::string(arg.getName());
				AllocaInst *slot  = Builder->CreateAlloca(
					arg.getType(), nullptr, pname);
				Builder->CreateStore(&arg, slot);
				NamedValues[pname] = slot;
				idx++;
			}

			// generate body
			stmt_gen(node->fn.body);

			// add implicit return 0 if no terminator
			if (!Builder->GetInsertBlock()->getTerminator())
				Builder->CreateRet(ConstantInt::get(ret_type, 0));

			// restore insert point to caller
			if (saved_bb)
				Builder->SetInsertPoint(saved_bb);

			return ConstantInt::get(Builder->getInt32Ty(), 0);
		}

		case NODE_CALL: {
			if (node->call.callee->kind != NODE_IDENT) {
				fprintf(stderr, "codegen: call target must be identifier\n");
				return nullptr;
			}

			std::string name = sym_name(node->call.callee->ident.sym);
			Function   *fn   = TheModule->getFunction(name);

			if (!fn) {
				fprintf(stderr, "codegen: undefined function '%s'\n", name.c_str());
				return nullptr;
			}

			if (fn->arg_size() != node->call.arg_count) {
				fprintf(stderr, "codegen: '%s' expects %zu args, got %u\n",
						name.c_str(), fn->arg_size(), node->call.arg_count);
				return nullptr;
			}

			std::vector<Value*> args;
			for (uint32_t i = 0; i < node->call.arg_count; i++) {
				Value *v = expr_gen(node->call.args[i]);
				if (!v) return nullptr;
				args.push_back(v);
			}

			return Builder->CreateCall(fn, args, "call_" + name);
		}

		case NODE_RETURN: {
			if (node->return_.value) {
				Value *val = expr_gen(node->return_.value);
				if (!val) return nullptr;

				// get the function's return type and cast if needed
				Function *fn      = Builder->GetInsertBlock()->getParent();
				Type     *ret_ty  = fn->getReturnType();

				if (val->getType() != ret_ty) {
					if (ret_ty->isDoubleTy() && val->getType()->isIntegerTy())
						val = Builder->CreateSIToFP(val, ret_ty);
					else if (ret_ty->isIntegerTy() && val->getType()->isDoubleTy())
						val = Builder->CreateFPToSI(val, ret_ty);
					else
						val = Builder->CreateIntCast(val, ret_ty, true);
				}

				Builder->CreateRet(val);
			} else {
				Builder->CreateRetVoid();
			}

			// unreachable block for code after return
			Function   *fn   = Builder->GetInsertBlock()->getParent();
			BasicBlock *dead = BasicBlock::Create(*TheContext, "dead", fn);
			Builder->SetInsertPoint(dead);
			return ConstantInt::get(Builder->getInt32Ty(), 0);
		}

		case NODE_EDIT: {
			Node *target = node->edit.target;

			if (target->kind == NODE_KEYWORD) {
				uint32_t tk_type = target->keyword.tk_type;

				KeywordMeta *meta = get_keyword_meta(tk_type);
				if (!meta) {
					fprintf(stderr, "codegen: unknown keyword type for edit\n");
					return nullptr;
				}

				Symbol kw_sym;
				kw_sym.start  = meta->name;
				kw_sym.length = strlen(meta->name);
				symtable_hash(&kw_sym);
				SymId kw_id = intern_symbol(TheSymtable, &kw_sym);

				auto it = KeywordModifiers.find(kw_id);
				KeywordInstance *inst;
				if (it != KeywordModifiers.end()) {
					inst = it->second;
				} else {
					inst = create_keyword_instance(tk_type);
					KeywordModifiers[kw_id] = inst;
				}

				for (uint32_t i = 0; i < node->edit.pair_count; i++) {
					Node *pair = node->edit.pairs[i];

					std::string attr_name;
					if (pair->edit_pair.field_sym != 0) {
						const Symbol *s = &TheSymtable->syms[pair->edit_pair.field_sym];
						attr_name = std::string(s->start, s->length);
					} else if (meta->attr_count > 0) {
						attr_name = meta->attributes[0].name;
					}

					if (attr_name.empty()) continue;

					if (pair->edit_pair.value->kind == NODE_STRING_LIT) {
						const Symbol *s = &TheSymtable->syms[pair->edit_pair.value->string_lit.sym];
						std::string val(s->start, s->length);
						apply_keyword_edit(inst, attr_name.c_str(), val.c_str());
					} else if (pair->edit_pair.value->kind == NODE_IDENT) {
						const Symbol *s = &TheSymtable->syms[pair->edit_pair.value->ident.sym];
						std::string val(s->start, s->length);
						apply_keyword_edit(inst, attr_name.c_str(), val.c_str());
					} else if (pair->edit_pair.value->kind == NODE_BOOL_LIT) {
						std::string val = pair->edit_pair.value->bool_lit.value ? "true" : "false";
						apply_keyword_edit(inst, attr_name.c_str(), val.c_str());
					}
				}

				return ConstantInt::get(Builder->getInt32Ty(), 0);
			}

			// resolve the target alloca 
			if (target->kind != NODE_IDENT) {
				fprintf(stderr, "codegen: edit target must be identifier or keyword\n");
				return nullptr;
			}

			std::string name = sym_name(target->ident.sym);
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

