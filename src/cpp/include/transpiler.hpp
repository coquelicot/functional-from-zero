#ifndef __TRANSPILER_H__
#define __TRANSPILER_H__

#include "ast.hpp"
#include <string>
#include <sstream>
#include <ostream>
#include <set>
#include <vector>
#include <map>
#include <memory>

struct code_func_t;
struct code_block_t {

    std::string retv;
    std::string code;
    std::vector<std::shared_ptr<code_func_t>> deps;

    code_block_t(const std::string &_retv, const std::string &_code, std::vector<std::shared_ptr<code_func_t>> &_deps);
};

struct code_func_t {

    std::string name;
    std::vector<std::string> envs;
    code_block_t body;

    code_func_t(const std::string &_name, const std::vector<std::string> &_envs, code_block_t &_body);
};

struct transpiler_t {

    unsigned long ident_cnt;
    std::set<std::string> builtins;
    std::map<std::string, std::string> ident_map;

    transpiler_t(std::set<std::string> &_builtins);

    std::string gen_ident();
    void transpile(node_hdr_t node, std::ostream &stm);

    void __transpile(
        node_hdr_t node,
        std::string &retv,
        std::stringstream &code,
        std::set<std::string> &envs,
        std::vector<std::shared_ptr<code_func_t>> &deps);
    void __emit(code_block_t &block, std::ostream &stm);
};

#endif
