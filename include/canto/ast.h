#pragma once

#include "canto/span.h"
#include "canto/token.h"
typedef enum {
#define AST_NODE(kind, name)	NODE_##kind,
#include "private/ast_kinds.def"
#undef AST_NODE
} NodeKind;

typedef struct Node Node;

struct Node {
	NodeKind kind;
	Span span;

	union {

		// Binary operations
		struct {
			Node*	  left;
			Node*	  right;
			TokenKind op;
		} binary;

		// Unary operation
		struct {
			Node*	  expr;
			TokenKind op;
		} unary;

		// callee: callee(args)
		struct {
			Node*	 callee;
			Node**	 args;
			uint32_t arg_count;
		} call;

		// Dot notations
		struct {
			Node*	left;
			uint32_t field_sym;
		} dot;

		// Index
		struct {
			Node* left;
			Node* index;
		} index;

		struct {
			Node* expr;
		} group;

		// notation: target.edit { }
		struct {
			Node*	 target;
			Node**   pairs;
			uint32_t pair_count;
		} edit;

		struct {
			uint32_t field_sym;
			Node*    value;
			bool	 is_parent;
		} edit_pair;
		
		struct {
            TokenKind op;
            Node*     expr;
            bool      is_parent; 
        } relative;

		// if condition
		struct {
            Node* cond;
            Node* then_;
            Node* else_; 
			bool is_loop;
        } if_;

		struct {
			Node* cond;
			Node* count;
			Node* body;
			bool is_infinite;
		} loop;

		// when (switch)
		struct {
            Node*	 subject;
            Node**   arms;
            uint32_t arm_count;
        } when;

		struct {
            Node* pattern; 
            Node* body;
            bool  is_else;
        } when_arm;

		// try
		struct {
			uint32_t label_sym;  
			Node*    body;     
		} try_scope;

		// optional 
		struct {
			uint32_t type_sym;
			uint32_t name_sym;
		} optional_decl;

		struct {
			uint32_t sym;
		} type_kw;

		// blocks { ... }
		struct {
            Node**   stmts;
            uint32_t count;
        } block;

		struct {
			struct Node** exprs;
			uint32_t count;
		} write;

		// let 
		struct {
            uint32_t name_sym;
            Node*    type_ann; 
            Node*    value;
            bool     is_fn;  
        } let;

		// when let: is_fn is true
		// it's store here as function
		struct {
            uint32_t  name_sym;
            Node**    params;
            uint32_t  param_count;
            Node*     body;
            Node*     return_type;
        } fn;

		struct {
            uint32_t  name_sym;
            uint32_t  parent_sym;  // 0 = no parent 
            Node**    members;
            uint32_t  member_count;
        } design;

		struct {
            uint32_t name_sym;
            Node*    type_ann;
            Node*    default_val;  // NULL = required
        } param;

		struct {
            uint32_t sym;
        } ident;

		struct {
            int64_t value;
        } int_lit;

        struct {
            double value;
        } double_lit;

        struct {
            uint32_t sym;
            bool     interpolated;
        } string_lit;

        struct {
            bool value;
        } bool_lit;

		struct {
			Node* value;          // NULL = bare return
		} return_;

		struct {
			uint32_t label_sym;   // 0 = closest enclosing loop, otherwise symbol id 
		} continue_;

		struct {
			uint32_t label_sym;   // 0 = closest enclosing loop, otherwise symbol id
		} break_;
	};
	
};

