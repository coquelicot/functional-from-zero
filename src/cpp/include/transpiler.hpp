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

struct code_lmb_t;
struct code_inst_t {

    enum type_t {
        APPLY,
        LAMBDA,
    };

    int type;
    std::string retv;
    std::string func;
    std::string arg;
    std::shared_ptr<code_lmb_t> lmb;
    std::vector<std::string> envs;
};

struct code_block_t {

    std::string retv;
    std::vector<code_inst_t> insts;
    std::vector<std::shared_ptr<code_lmb_t>> deps;

    code_block_t(std::string &_retv, std::vector<code_inst_t> &_insts, std::vector<std::shared_ptr<code_lmb_t>> &_deps);
};

struct code_lmb_t {

    std::string name;
    std::string arg;
    std::vector<std::string> envs;
    code_block_t body;

    code_lmb_t(std::string &_name, std::string &_arg, std::vector<std::string> &_envs, code_block_t &_body);
};

struct transpiler_t {

    unsigned long global_id;
    std::set<std::string> builtins;
    std::map<std::string, std::string> ident_map;

    transpiler_t(std::set<std::string> &_builtins);

    std::string next_ident();
    void transpile(node_hdr_t node, std::ostream &stm);

    void __transpile(
        node_hdr_t node,
        std::string &retv,
        std::vector<code_inst_t> &insts,
        std::set<std::string> &envs,
        std::vector<std::shared_ptr<code_lmb_t>> &deps);
    void __emit(code_lmb_t &block, std::ostream &stm);
};

#endif
