#include "context.hpp"

std::unique_ptr<LLVMContext> TheContext;
std::unique_ptr<IRBuilder<>> Builder;
std::unique_ptr<Module> TheModule;
std::map<std::string, AllocaInst*> NamedValues;
SymTable *TheSymtable = nullptr;
std::vector<std::pair<BasicBlock*, BasicBlock*>> LoopStack;

std::string sym_name(uint32_t sym_id) {
    if (!TheSymtable || sym_id == 0 || sym_id > TheSymtable->count)
        return "<unknown_" + std::to_string(sym_id) + ">";
    const Symbol *s = &TheSymtable->syms[sym_id];
    return std::string(s->start, s->length);
}

