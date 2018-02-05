#ifndef __TOKENIZER_H__
#define __TOKENIZER_H__

#include <string>
#include <utility>
#include <istream>
#include <queue>

struct token_t {

    enum type_t {
        LEFT_BRACKET,
        RIGHT_BRACKET,
        BACK_SLASH,
        IDENTIFIER,
        END,
    };

    type_t type;
    std::string ident;
};

struct tokenizer_t {

    std::istream &stm;
    std::queue<token_t> buf;

    tokenizer_t(std::istream&);

    token_t pop();
    token_t peak();

    bool __parse_more();
    static bool __is_comment(std::string&);
};

#endif
