#include<stdio.h>
#include<stdlib.h>

#include "canto/compiler.h"
#include "canto/ast.h"
#include "canto/diagnostic.h"
#include "canto/jit.h"
#include "canto/lexer.h"
#include "canto/parser.h"
#include "canto/source_map.h"
#include "canto/codegen.h"
#include "canto/repl.h"

static bool is_expr_node(NodeKind kind) {
    switch (kind) {
        case NODE_BINARY:
        case NODE_UNARY:
        case NODE_CALL:
        case NODE_IDENT:
        case NODE_INT_LIT:
        case NODE_DOUBLE_LIT:
        case NODE_STRING_LIT:
        case NODE_BOOL_LIT:
        case NODE_GROUP:
        case NODE_BLOCK:
        case NODE_DOT:
        case NODE_DOT_DOT:
            return true;
        default:
            return false;
    }
}

CantoContext* canto_ctx_create(bool is_repl) {
    CantoContext* ctx = (CantoContext*)malloc(sizeof(CantoContext));
    if (!ctx) return NULL;
    
    ctx->is_repl = is_repl;
    symtable_init(&ctx->global_symbols);
    if (is_repl) repl_init();
    codegen_init();
	if(is_repl) jit_init();
    return ctx;
}

void canto_ctx_free(CantoContext* ctx) {
    if (!ctx) return;
	if (ctx->is_repl){
		jit_free();
		repl_free();
	} 
    symtable_free(&ctx->global_symbols);
    codegen_free();
    free(ctx);
}

bool compile(CantoContext* ctx, const char* source, const char* file_path, const char* output_path) {
    Lexer lexer;
    DiagEngine diags;
    SourceMap map;
    Parser parser;
    bool success = true;
    
    diag_init(&diags);
    init_source_map(&map, file_path, source);

    init_lexer(&lexer, &map);

    lexer.symbols = ctx->global_symbols;

    run_lex(&lexer, &diags);

    if (diag_has_errors(&diags)) {
        diag_render_report(&diags, &map);
        success = false;
        goto cleanup;
    }

    init_parser(&parser, &diags, lexer.tokens, lexer.tk_count, &map);
    Node *tree = parse_program(&parser);

    if (diag_has_errors(&diags) && !tree) {
        diag_render_report(&diags, &map);
        success = false;
        goto cleanup;
    }

    codegen_set_symtable(&lexer.symbols);
    if (ctx->is_repl) repl_setup_globals();

    bool ok = true;
    for (uint32_t i = 0; i < tree->block.count; i++) {
        if (codegen_eval_expr(tree->block.stmts[i]) != 0){
            ok = false;
            break;
        } 
    }

    if (!ok){
        success = false;
        if (ctx->is_repl) {
            codegen_init();
            codegen_set_symtable(&ctx->global_symbols);
        } else {
            codegen_finalize(1);
        }
    } else {
        bool is_expr = ctx->is_repl &&
                       tree->block.count > 0 &&
                       is_expr_node(tree->block.stmts[tree->block.count - 1]->kind);

        if (ctx->is_repl && is_expr) {
            codegen_finalize_repl();
        } else {
            codegen_finalize(0);
        }

        if (!ctx->is_repl) {
            if (output_path != NULL) {
                codegen_dump(output_path);
            }
        } else {
            repl_register_storage();
            if (jit_run() == -1) success = false;
            else if (is_expr) repl_print(REPL_RESULT_SLOT);

            codegen_init();
            codegen_set_symtable(&ctx->global_symbols);
        }
    }

    ctx->global_symbols = lexer.symbols;

cleanup:
    free_parser(&parser);
    free(lexer.tokens);
    diag_free(&diags);

    return success;
}
