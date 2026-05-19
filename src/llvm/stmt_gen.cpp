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

