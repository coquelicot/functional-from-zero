#include "transpiler.hpp"
#include <sstream>
#include <iostream>
#include <cassert>

code_block_t::code_block_t(std::string &_retv, std::vector<code_inst_t> &_insts, std::vector<std::shared_ptr<code_lmb_t>> &_deps) :
    retv(_retv), insts(_insts), deps(_deps) {};

code_lmb_t::code_lmb_t(std::string &_name, std::string &_arg, std::vector<std::string> &_envs, code_block_t &_body) :
    name(_name), arg(_arg), envs(_envs), body(_body) {}

transpiler_t::transpiler_t(std::set<std::string> &_builtins)
    : builtins(_builtins) {}

std::string transpiler_t::next_ident() {
    std::stringstream ss;
    ss << "_" << global_id++;
    return ss.str();
}

void transpiler_t::transpile(node_hdr_t node, std::ostream &stm) {

    global_id = 1;
    ident_map.clear();
    std::vector<code_inst_t> insts;
    std::set<std::string> envs;
    std::vector<std::shared_ptr<code_lmb_t>> deps;

    std::string name = next_ident();
    std::string arg = next_ident();
    std::string retv = next_ident();
    __transpile(node, retv, insts, envs, deps);

    std::vector<std::string> _envs;
    code_block_t block(retv, insts, deps);
    code_lmb_t prog(retv, arg, _envs, block);

    stm << "#include \"runtime.hpp\"\n";
    __emit(prog, stm);
    stm << "int main() {\n"
        << "  std::make_shared<" << prog.name << ">()->exec(nullptr);\n"
        << "}\n";
}

void transpiler_t::__emit(code_lmb_t &lmb, std::ostream &stm) {

    for (auto dep : lmb.body.deps)
        __emit(*dep, stm);

    // header
    stm << "struct " << lmb.name << " : public lmb_t {\n";

    // eval
    stm << "  std::shared_ptr<lmb_t> exec(std::shared_ptr<lmb_t> " << lmb.arg << ") {\n";
    for (auto inst : lmb.body.insts) {
        stm << "    auto " << inst.retv << " = ";
        if (inst.type == code_inst_t::APPLY) {
            stm << inst.func << "->exec(" << inst.arg << ");\n";
        } else {
            stm << "std::make_shared<" << inst.lmb->name << ">(";
            for (size_t i = 0; i < inst.envs.size(); i++) {
                if (i > 0)
                    stm << ", ";
                stm << inst.envs[i];
            }
            stm << ");\n";
        }
    }
    stm << "    return " << lmb.body.retv << ";\n";
    stm << "  };\n";

    // constructor
    stm << "  " << lmb.name << "(";
    for (size_t i = 0; i < lmb.envs.size(); i++) {
        if (i > 0)
            stm << ", ";
        stm << "std::shared_ptr<lmb_t> _" << lmb.envs[i];
    }
    stm << ")";
    for (size_t i = 0; i < lmb.envs.size(); i++) {
        stm << (i > 0 ? ", " : " : ");
        stm << lmb.envs[i] << "(_" << lmb.envs[i] << ")";
    }
    stm << " {}\n";

    // vars
    if (lmb.envs.size() > 0) {
        stm << "  std::shared_ptr<lmb_t> ";
        for (size_t i = 0; i < lmb.envs.size(); i++) {
            if (i > 0)
                stm << ", ";
            stm << lmb.envs[i];
        }
        stm << ";\n";
    }

    // end
    stm << "};\n";
}

void transpiler_t::__transpile(
    node_hdr_t _node,
    std::string &retv,
    std::vector<code_inst_t> &insts,
    std::set<std::string> &envs,
    std::vector<std::shared_ptr<code_lmb_t>> &deps)
{

    try {
        term_node_t &node = dynamic_cast<term_node_t&>(*_node);

        if (builtins.count(node.ident)) {
            retv = node.ident;
        } else {
            assert(ident_map.count(node.ident));
            retv = ident_map[node.ident];
            envs.insert(retv);
        }

        return;
    } catch (std::bad_cast e) {}

    try {
        apply_node_t &node = dynamic_cast<apply_node_t&>(*_node);

        std::string func = next_ident();
        std::string arg = next_ident();
        __transpile(node.nd_fun, func, insts, envs, deps);
        __transpile(node.nd_arg, arg, insts, envs, deps);
        insts.push_back(code_inst_t{code_inst_t::APPLY, retv, func, arg, nullptr, {}});

        return;
    } catch (std::bad_cast e) {}

    try {
        lmb_node_t &node = dynamic_cast<lmb_node_t&>(*_node);

        bool new_ident = !ident_map.count(node.arg);
        if (new_ident)
            ident_map[node.arg] = next_ident();

        std::string name = next_ident();
        std::string arg = ident_map[node.arg];
        std::string lmb_retv = next_ident();
        std::vector<code_inst_t> lmb_insts;
        std::set<std::string> lmb_envs;
        std::vector<std::shared_ptr<code_lmb_t>> lmb_deps;

        __transpile(node.nd_body, lmb_retv, lmb_insts, lmb_envs, lmb_deps);

        std::vector<std::string> venvs;
        for (auto env : lmb_envs)
            if (env != arg) {
                venvs.push_back(env);
                envs.insert(env);
            }

        code_block_t block{lmb_retv, lmb_insts, lmb_deps};
        deps.push_back(std::make_shared<code_lmb_t>(name, arg, venvs, block));

        insts.push_back(code_inst_t{code_inst_t::LAMBDA, retv, "", "", deps.back(), venvs});

        if (new_ident)
            ident_map.erase(node.arg);

        return;
    } catch (std::bad_cast e) {}

    assert(false);
}
