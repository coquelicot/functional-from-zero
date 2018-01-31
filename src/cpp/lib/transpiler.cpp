#include "transpiler.hpp"
#include <sstream>
#include <cassert>
#include <iostream>

code_block_t::code_block_t(const std::string &_retv, const std::string &_code, std::vector<std::shared_ptr<code_func_t>> &_deps) :
    retv(_retv), code(_code), deps(_deps) {}

code_func_t::code_func_t(const std::string &_name, const std::vector<std::string> &_envs, code_block_t &_body) :
    name(_name), envs(_envs), body(_body) {}

transpiler_t::transpiler_t(std::set<std::string> &_builtins)
    : ident_cnt(0), builtins(_builtins) {}

std::string transpiler_t::gen_ident() {
    std::stringstream ss;
    ss << "_" << ++ident_cnt;
    return ss.str();
}

void transpiler_t::transpile(node_hdr_t node, std::ostream &stm) {

    std::string retv = gen_ident();
    std::stringstream code;
    std::set<std::string> envs;
    std::vector<std::shared_ptr<code_func_t>> deps;

    code << "int main() {\n";
    __transpile(node, retv, code, envs, deps);
    code << "}\n";

    for (auto env : envs) {
        std::cerr << env << std::endl;
        assert(builtins.count(env));
    }

    code_block_t prog(retv, code.str(), deps);

    stm << "#include \"runtime.hpp\"\n";
    __emit(prog, stm);
}

void transpiler_t::__emit(code_block_t &block, std::ostream &stm) {
    for (auto dep : block.deps)
        __emit(dep->body, stm);
    stm << block.code;
}

void transpiler_t::__transpile(
    node_hdr_t _node,
    std::string &retv,
    std::stringstream &code,
    std::set<std::string> &envs,
    std::vector<std::shared_ptr<code_func_t>> &deps)
{

    try {
        term_node_t &node = dynamic_cast<term_node_t&>(*_node);

        std::string ident;
        if (builtins.count(node.ident)) {
            ident = node.ident;
        } else {
            if (!ident_map.count(node.ident))
                ident_map[node.ident] = gen_ident();
            ident = ident_map[node.ident];
            envs.insert(ident);
        }

        retv = ident;
        return;
    } catch (std::bad_cast e) {}

    try {
        apply_node_t &node = dynamic_cast<apply_node_t&>(*_node);

        std::string fun = gen_ident();
        std::string arg = gen_ident();
        __transpile(node.nd_fun, fun, code, envs, deps);
        __transpile(node.nd_arg, arg, code, envs, deps);
        code << "auto " << retv << " = " << fun << "->exec(" << arg << ");\n";

        return;
    } catch (std::bad_cast e) {}

    try {
        lmb_node_t &node = dynamic_cast<lmb_node_t&>(*_node);

        if (!ident_map.count(node.arg))
            ident_map[node.arg] = gen_ident();

        std::string name = gen_ident();
        std::string arg = ident_map[node.arg];
        std::string lmb_retv = gen_ident();
        std::stringstream lmb_code;
        std::set<std::string> lmb_envs;
        std::vector<std::shared_ptr<code_func_t>> lmb_deps;

        lmb_code << "struct " << name << " : public lmb_t {\n";

        // exec
        lmb_code << "std::shared_ptr<lmb_t> exec(std::shared_ptr<lmb_t> " << arg << ") {\n";
        __transpile(node.nd_body, lmb_retv, lmb_code, lmb_envs, lmb_deps);
        lmb_envs.erase(arg);
        lmb_code << "return " << lmb_retv << ";\n";
        lmb_code << "};\n";

        // constructor
        std::vector<std::string> lmb_venvs(lmb_envs.begin(), lmb_envs.end());
        lmb_code << name << "(";
        for (size_t i = 0; i < lmb_venvs.size(); i++) {
            if (i > 0)
                lmb_code << ", ";
            lmb_code << "std::shared_ptr<lmb_t> &" << "_" << lmb_venvs[i];
        }
        lmb_code << ")";
        for (size_t i = 0; i < lmb_venvs.size(); i++) {
            lmb_code << (i == 0 ? " : " : ", ");
            lmb_code << lmb_venvs[i] << "(_" << lmb_venvs[i] << ")";
        }
        lmb_code << " {};\n";

        // member
        for (size_t i = 0; i < lmb_venvs.size(); i++)
            lmb_code << "std::shared_ptr<lmb_t> " << lmb_venvs[i] << ";\n";

        lmb_code << "};\n";

        // code

        code << "auto " << retv << " = std::make_shared<" << name << ">(";
        for (size_t i = 0; i < lmb_venvs.size(); i++) {
            if (i != 0)
                code << ", ";
            code << lmb_venvs[i];
        }
        code << ");\n";

        code_block_t block{lmb_retv, lmb_code.str(), lmb_deps};
        deps.push_back(std::make_shared<code_func_t>(name, lmb_venvs, block));
        for (auto env : lmb_envs)
            envs.insert(env);

        return;
    } catch (std::bad_cast e) {}

    assert(false);
}
