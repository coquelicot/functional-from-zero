#include "ast.hpp"
#include <iostream>

apply_node_t::apply_node_t(node_hdr_t _nd_fun, node_hdr_t _nd_arg) :
    nd_fun(_nd_fun), nd_arg(_nd_arg) {}

void apply_node_t::print(int depth) {
    try {
       dynamic_cast<apply_node_t&>(*nd_fun);
        nd_fun->print(depth);
        nd_arg->print(depth + 1);
    } catch (std::bad_cast e) {
        std::cerr << std::string(depth, ' ') << "apply:" << std::endl;
        nd_fun->print(depth + 1);
        nd_arg->print(depth + 1);
    }
}

term_node_t::term_node_t(std::string &_ident) :
    ident(_ident) {}

void term_node_t::print(int depth) {
    std::cerr << std::string(depth, ' ') << "ref: " << ident << std::endl;
}

lmb_node_t::lmb_node_t(std::string &_arg, node_hdr_t _nd_body) :
    arg(_arg), nd_body(_nd_body) {}

void lmb_node_t::print(int depth) {
    std::cerr << std::string(depth, ' ') << "lmb(" << arg << "):" << std::endl;
    nd_body->print(depth + 1);
}
