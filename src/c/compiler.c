#include<limits.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<dirent.h>
#include<sys/stat.h>

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

// Absolute paths of modules loaded by `get file` during this compile.
// Each module is evaluated once, which also stops import cycles.
static char**   loaded_modules;
static uint32_t loaded_count;

static bool eval_stmts(Node* tree, const char* file_path, SymTable* syms);

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char* buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

// Compiles one module file, unless it was already loaded. Takes `real`.
static bool load_file(char* real, SymTable* syms) {
    for (uint32_t i = 0; i < loaded_count; i++) {
        if (strcmp(loaded_modules[i], real) == 0) { free(real); return true; }
    }
    loaded_modules = realloc(loaded_modules, (loaded_count + 1) * sizeof(char*));
    loaded_modules[loaded_count++] = real;

    // ponytail: the source, tokens and AST are never freed. Symbols point into
    // the source, and codegen may hold AST nodes until finalize.
    char* source = read_file(real);
    if (!source) {
        fprintf(stderr, "Compiler Error: cannot read module '%s'\n", real);
        return false;
    }

    Lexer lexer;
    DiagEngine diags;
    SourceMap* map = malloc(sizeof(SourceMap));
    Parser* parser = malloc(sizeof(Parser));

    diag_init(&diags);
    init_source_map(map, real, source);
    init_lexer(&lexer, map);
    symtable_free(&lexer.symbols);
    lexer.symbols = *syms;
    run_lex(&lexer, &diags);
    *syms = lexer.symbols;  // interning may have grown the shared table

    bool ok = !diag_has_errors(&diags);
    Node* tree = NULL;
    if (ok) {
        init_parser(parser, &diags, lexer.tokens, lexer.tk_count, map, syms);
        tree = parse_program(parser);
        ok = tree && !diag_has_errors(&diags);
    }
    if (!ok) diag_render_report(&diags, map);
    diag_free(&diags);

    return ok && eval_stmts(tree, real, syms);
}

// get editor → every .ct file in the folder, in name order
static bool load_dir(const char* dir, SymTable* syms) {
    struct dirent** list;
    int n = scandir(dir, &list, NULL, alphasort);
    if (n < 0) return false;
    bool ok = true;
    for (int i = 0; i < n; i++) {
        const char* name = list[i]->d_name;
        size_t len = strlen(name);
        if (ok && len > 3 && strcmp(name + len - 3, ".ct") == 0) {
            char path[PATH_MAX];
            snprintf(path, sizeof path, "%s/%s", dir, name);
            char* real = realpath(path, NULL);
            ok = real && load_file(real, syms);
        }
        free(list[i]);
    }
    free(list);
    return ok;
}

// get utils → compiles <importer's dir>/utils.ct in place, or every .ct
// file in <importer's dir>/utils/ when that's a folder
static bool load_module(Node* get, const char* importer, SymTable* syms) {
    const Symbol* s = &syms->syms[get->get.lib_sym];
    int dir_len = 0;
    if (importer && s->start[0] != '/') {
        const char* slash = strrchr(importer, '/');
        if (slash) dir_len = (int)(slash - importer + 1);
    }
    bool has_ext = s->length >= 3 && memcmp(s->start + s->length - 3, ".ct", 3) == 0;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%.*s%.*s%s", dir_len, importer, (int)s->length, s->start, has_ext ? "" : ".ct");

    char* real = realpath(path, NULL);
    if (real) return load_file(real, syms);

    if (!has_ext) {
        path[strlen(path) - 3] = '\0';  // drop ".ct"
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return load_dir(path, syms);
    }
    fprintf(stderr, "Compiler Error: cannot find module '%s'\n", path);
    return false;
}

static bool eval_stmts(Node* tree, const char* file_path, SymTable* syms) {
    for (uint32_t i = 0; i < tree->block.count; i++) {
        Node* stmt = tree->block.stmts[i];
        bool ok = stmt->kind == NODE_GET && stmt->get.is_canto
                ? load_module(stmt, file_path, syms)
                : codegen_eval_expr(stmt) == 0;
        if (!ok) return false;
    }
    return true;
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

    init_parser(&parser, &diags, lexer.tokens, lexer.tk_count, &map, &lexer.symbols);
    Node *tree = parse_program(&parser);

    if (diag_has_errors(&diags)) {
        diag_render_report(&diags, &map);
        success = false;
        goto cleanup;
    }

    codegen_set_symtable(&lexer.symbols);
    if (ctx->is_repl) repl_setup_globals();

    for (uint32_t i = 0; i < loaded_count; i++) free(loaded_modules[i]);
    loaded_count = 0;
    // a module importing the main file back must not run it again
    char* main_real = file_path ? realpath(file_path, NULL) : NULL;
    if (main_real) {
        loaded_modules = realloc(loaded_modules, sizeof(char*));
        loaded_modules[loaded_count++] = main_real;
    }

    bool ok = eval_stmts(tree, file_path, &lexer.symbols);

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
