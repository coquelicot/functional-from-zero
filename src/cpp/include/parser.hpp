#ifndef __PARSER_H__
#define __PARSER_H__

#include "tokenizer.hpp"
#include "ast.hpp"
#include <string>
#include <stack>

struct parser_t {

    struct result_t {

        enum type_t {
            DONE,
            CONTINUE,
            ERROR,
        };

        type_t type;
        node_hdr_t node;
    };

    struct state_t {

        enum type_t {
            ROOT,
            EXPR,
            LMB_IDENT,
            LMB_EXPR,
        };

        type_t type;
        std::string ident;
        node_hdr_t node;
    };

    std::stack<state_t> states;

    parser_t();
    result_t shift(token_t);
};

#endif
