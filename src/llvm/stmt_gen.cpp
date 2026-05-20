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

			 for (uint32_t i = 0; i < node->write.count; ++i) {
				Node* arg = node->write.exprs[i];
				Value* val = expr_gen(arg);
				if (!val) continue;

				if (arg->kind == NODE_STRING_LIT){
					Value* fmt = Builder->CreateGlobalStringPtr("%s", "fmt_str");
					Builder->CreateCall(printf_fn, { fmt, val });
				}
				else if (val->getType()->isIntegerTy()) {
					Value* fmt = Builder->CreateGlobalStringPtr("%lld", "fmt_i64");
					Builder->CreateCall(printf_fn, { fmt, val });
				}
				else if (val->getType()->isDoubleTy()) {
					Value *fmt = Builder->CreateGlobalStringPtr("%f", "fmt_f64");
					Builder->CreateCall(printf_fn, { fmt, val });
				}
				else if (val->getType()->isIntegerTy(1)) { // bool
					Value *fmt  = Builder->CreateGlobalStringPtr("%s", "fmt_bool");
					Value *tstr = Builder->CreateGlobalStringPtr("true",  "true_str");
					Value *fstr = Builder->CreateGlobalStringPtr("false", "false_str");
					Value *sel  = Builder->CreateSelect(val, tstr, fstr, "bool_str");
					Builder->CreateCall(printf_fn, { fmt, sel });
				}
				else {
					Value *fmt = Builder->CreateGlobalStringPtr("%d", "fmt_int");
					Value *cast = Builder->CreateIntCast(
						val, Builder->getInt32Ty(), true);
					Builder->CreateCall(printf_fn, { fmt, cast });
				}

			}

			// Add new line
			Value *newline = Builder->CreateGlobalStringPtr("\n", "newline");
			Builder->CreateCall(printf_fn, { newline });

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

