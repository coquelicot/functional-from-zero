#include "tokenizer.hpp"
#include "parser.hpp"
#include "transpiler.hpp"
#include <fstream>
#include <iostream>
#include <cassert>

int main(int argc, char *args[]) {

    assert(argc > 2);
    std::fstream fin(args[1]);
    std::fstream fout(args[2], std::fstream::out);

    std::set<std::string> builtins{
        "__builtin_g",
        "__builtin_p0",
        "__builtin_p1",
    };

    tokenizer_t tokenizer(fin);
    parser_t parser;
    transpiler_t transpiler(builtins);

    while (true) {

        parser_t::result_t result;
        do {
            token_t token = tokenizer.pop();
            result = parser.shift(token);
        } while (result.type == parser_t::result_t::CONTINUE);

        if (result.type == parser_t::result_t::ERROR) {
            std::cerr << "error" << std::endl;
            parser = parser_t();
            continue;
        }
        if (result.node == nullptr)
            break;

        //result.node->print();
        transpiler.transpile(result.node, fout);
    }
}
