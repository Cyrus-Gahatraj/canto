#include<stdio.h>
#include<stdlib.h>

#include "canto/compiler.h"
#include "canto/ast.h"
#include "canto/diagnostic.h"
#include "canto/lexer.h"
#include "canto/parser.h"
#include "canto/source_map.h"
#include "canto/codegen.h"

CantoContext* canto_ctx_create(bool is_repl) {
    CantoContext* ctx = (CantoContext*)malloc(sizeof(CantoContext));
    if (!ctx) return NULL;
    
    ctx->is_repl = is_repl;
    symtable_init(&ctx->global_symbols);
    codegen_init();
    return ctx;
}

void canto_ctx_free(CantoContext* ctx) {
    if (!ctx) return;
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

    // Bind context's persistent symbols to the local lexer
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

    bool ok = true;
    for (uint32_t i = 0; i < tree->block.count; i++) {
        if (codegen_eval_expr(tree->block.stmts[i]) != 0){
            ok = false;
            break;
        } 
    }

    if (!ok){
        success = false;
        codegen_finalize(1);
    } else {
        if (!ctx->is_repl) {
            codegen_finalize(0);
            if (output_path != NULL) {
                codegen_dump(output_path);
            }
        } else {
            printf("\n");
            codegen_print_ir();
        }
    }

    // Save mutated symbol additions back into the persistent context
    ctx->global_symbols = lexer.symbols;

cleanup:
    free_parser(&parser);
    free(lexer.tokens);
    diag_free(&diags);

    return success;
}
