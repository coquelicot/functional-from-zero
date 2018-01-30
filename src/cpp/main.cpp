#include "tokenizer.hpp"
#include "parser.hpp"
#include <iostream>

int main() {

    tokenizer_t tokenizer(std::cin);
    parser_t parser;

    while (true) {

        parser_t::result_t result;
        do {
            token_t token = tokenizer.pop();
            result = parser.shift(token);
        } while (result.type == parser_t::result_t::CONTINUE);

        if (result.type == parser_t::result_t::ERROR)
            std::cerr << "error" << std::endl;
        else if (result.node)
            result.node->print();
        else
            break;
    }
}
