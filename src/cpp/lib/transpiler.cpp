#include "transpiler.hpp"
#include <sstream>
#include <iostream>
#include <stack>
#include <tuple>
#include <cassert>

struct code_lmb_t;

struct code_id_t {

    enum type_t {
        NONE,
        LOCAL,
        ENV,
        ARG,
        GLOBAL,
    };

    type_t type;
    int val;

    friend bool operator<(const code_id_t &a, const code_id_t &b) {
        return std::tie(a.type, a.val) < std::tie(b.type, b.val);
    }

    friend std::ostream& operator<<(std::ostream &stm, const code_id_t &id) {
        switch (id.type) {
            case code_id_t::GLOBAL:
                return stm << "_g" << id.val;
            case code_id_t::LOCAL:
                return stm << "_" << id.val;
            case code_id_t::ENV:
                return stm << "_e" << id.val;
            case code_id_t::ARG:
                return stm << "_a";
            default:
                assert(false);

        }
    }
};

struct code_inst_t {

    enum type_t {
        APPLY,
        LAMBDA,
    };

    type_t type;
    code_id_t retv;
    code_id_t func;
    code_id_t arg;
    std::shared_ptr<code_lmb_t> lmb;
    std::vector<code_id_t> envs;
};

struct code_block_t {
    code_id_t retv;
    std::vector<code_inst_t> insts;
    std::set<std::shared_ptr<code_lmb_t>> deps;
};

struct code_lmb_t {
    code_id_t name;
    code_id_t obj;
    int env_cnt;
    code_block_t body;
    int next_local_id;
    bool emited;
};

struct transpiler_t::impl_t {

    int global_id;
    std::map<std::string, code_id_t> builtins;
    std::map<std::string, int> ident_cnt;
    std::map<code_id_t, std::shared_ptr<code_lmb_t>> static_lmbs;

    impl_t(transpiler_t *parent) : global_id(0) {
        for (auto str : parent->builtins)
            builtins.insert(std::make_pair(str, next_global_id()));
    }

    code_id_t next_global_id() {
        return code_id_t{code_id_t::GLOBAL, global_id++};
    }

    code_id_t none_id() {
        return code_id_t{code_id_t::NONE, 0};
    }

    code_id_t arg_id() {
        return code_id_t{code_id_t::ARG, 0};
    }

    code_id_t local_id(int val) {
        return code_id_t{code_id_t::LOCAL, val};
    }

    code_id_t env_id(int val) {
        return code_id_t{code_id_t::ENV, val};
    }

    void transpile(node_hdr_t node, std::ostream &stm) {

        ident_cnt.clear();
        global_id = builtins.size();
        std::vector<code_inst_t> insts;
        std::map<std::string, code_id_t> envs;
        std::set<std::shared_ptr<code_lmb_t>> deps;

        code_id_t name = next_global_id();
        code_id_t obj = next_global_id();
        code_id_t retv = local_id(0);
        int next_local_id = 1;
        int next_env_id = 0;
        __transpile(node, next_local_id, next_env_id, retv, insts, envs, deps);

        code_block_t block{retv, insts, deps};
        std::shared_ptr<code_lmb_t> prog(new code_lmb_t{name, obj, 0, block, next_local_id, false});

        // optimize
        __inline_temp_lmb(prog);
        //__extract_static_lmb(prog);

        // output
        stm << "#include \"runtime.hpp\"\n";

        for (auto pair : builtins)
            stm << "auto& " << pair.second << " = " << pair.first << ";\n";

        __emit(*prog, stm);
        stm << "int main() {\n"
            << "  " << obj << "->exec(nullptr);\n"
            << "}\n";
    }

    void __emit(code_lmb_t &lmb, std::ostream &stm) {

        if (lmb.emited)
            return;
        lmb.emited = true;

        for (auto dep : lmb.body.deps)
            __emit(*dep, stm);

        // header
        stm << "struct " << lmb.name << " : public lmb_t {\n";

        // eval
        stm << "  std::shared_ptr<lmb_t> exec(std::shared_ptr<lmb_t> " << arg_id() << ") {\n";
        for (auto inst : lmb.body.insts) {
            stm << "    auto " << inst.retv << " = ";
            if (inst.type == code_inst_t::APPLY) {
                stm << inst.func << "->exec(" << inst.arg << ");\n";
            } else {
                stm << "std::make_shared<" << inst.lmb->name << ">(";
                for (auto it = inst.envs.begin(); it != inst.envs.end(); it++) {
                    if (it != inst.envs.begin())
                        stm << ", ";
                    stm << *it;
                }
                stm << ");\n";
            }
        }
        stm << "    return " << lmb.body.retv << ";\n";
        stm << "  };\n";

        // constructor
        stm << "  " << lmb.name << "(";
        for (int i = 0; i < lmb.env_cnt; i++) {
            if (i != 0)
                stm << ", ";
            stm << "std::shared_ptr<lmb_t> _" << env_id(i);
        }
        stm << ")";
        for (int i = 0; i < lmb.env_cnt; i++) {
            stm << (i != 0 ? ", " : " : ");
            stm << env_id(i) << "(_" << env_id(i) << ")";
        }
        stm << " {}\n";

        // vars
        if (lmb.env_cnt > 0) {
            stm << "  std::shared_ptr<lmb_t> ";
            for (int i = 0; i < lmb.env_cnt; i++) {
                if (i != 0)
                    stm << ", ";
                stm << env_id(i);
            }
            stm << ";\n";
        }

        // end
        stm << "};\n";
        if (lmb.obj.type != code_id_t::NONE)
            stm << "std::shared_ptr<lmb_t> " << lmb.obj << "(new " << lmb.name << "());\n";
    }

    void __transpile(
        node_hdr_t _node,
        int &next_local_id,
        int &next_env_id,
        code_id_t &retv,
        std::vector<code_inst_t> &insts,
        std::map<std::string, code_id_t> &envs,
        std::set<std::shared_ptr<code_lmb_t>> &deps)
    {

        try {
            term_node_t &node = dynamic_cast<term_node_t&>(*_node);

            if (builtins.count(node.ident)) {
                retv = builtins[node.ident];
            } else {
                assert(ident_cnt[node.ident]);
                if (!envs.count(node.ident))
                    envs.insert(std::make_pair(node.ident, env_id(next_env_id++)));
                retv = envs[node.ident];
            }

            return;
        } catch (std::bad_cast e) {}

        try {
            apply_node_t &node = dynamic_cast<apply_node_t&>(*_node);

            code_id_t func = next_global_id();
            code_id_t arg = local_id(next_local_id++);
            __transpile(node.nd_fun, next_local_id, next_env_id, func, insts, envs, deps);
            __transpile(node.nd_arg, next_local_id, next_env_id, arg, insts, envs, deps);
            insts.push_back(code_inst_t{code_inst_t::APPLY, retv, func, arg, nullptr, {}});

            return;
        } catch (std::bad_cast e) {}

        try {
            lmb_node_t &node = dynamic_cast<lmb_node_t&>(*_node);

            int lmb_next_local_id = 1;
            int lmb_next_env_id = 0;
            code_id_t name = next_global_id();
            code_id_t lmb_retv = local_id(0);
            std::vector<code_inst_t> lmb_insts;
            std::map<std::string, code_id_t> lmb_envs;
            std::set<std::shared_ptr<code_lmb_t>> lmb_deps;

            ident_cnt[node.arg]++;
            lmb_envs[node.arg] = arg_id();
            __transpile(node.nd_body, lmb_next_local_id, lmb_next_env_id, lmb_retv, lmb_insts, lmb_envs, lmb_deps);
            ident_cnt[node.arg]--;
            lmb_envs.erase(node.arg);

            code_block_t block{lmb_retv, lmb_insts, lmb_deps};
            std::shared_ptr<code_lmb_t> lmb(new code_lmb_t{name, none_id(), (int)lmb_envs.size(), block, lmb_next_local_id, false});
            deps.insert(lmb);

            std::vector<code_id_t> lmb_venvs(lmb_envs.size());
            for (auto pair : lmb_envs) {
                if (!envs.count(pair.first))
                    envs.insert(std::make_pair(pair.first, env_id(next_env_id++)));
                lmb_venvs[pair.second.val] = envs[pair.first];
            }
            insts.push_back(code_inst_t{code_inst_t::LAMBDA, retv, none_id(), none_id(), lmb, lmb_venvs});

            return;
        } catch (std::bad_cast e) {}

        assert(false);
    }

    void __extract_static_lmb(std::shared_ptr<code_lmb_t> lmb, std::map<code_id_t, std::shared_ptr<code_lmb_t>> alias={}, const std::map<code_id_t, code_id_t> &env_map={}) {

        std::vector<code_inst_t> ninsts;
        std::map<code_id_t, std::map<code_id_t, std::shared_ptr<code_lmb_t>>> lmb_aliases;
        std::map<code_id_t, std::map<code_id_t, code_id_t>> lmb_env_maps;

        for (auto &inst : lmb->body.insts) {

            if (inst.type == code_inst_t::LAMBDA) {

                // clean up params & build alias
                std::vector<code_id_t> nenvs;
                auto &lmb_alias = lmb_aliases[inst.retv];
                auto &lmb_env_map = lmb_env_maps[inst.retv];
                for (int i = 0, j = 0; i < (int)inst.envs.size(); i++) {
                    if (alias.count(inst.envs[i])) {
                        lmb_alias[env_id(i)] = alias[inst.envs[i]];
                    } else {
                        if (env_map.count(inst.envs[i]))
                            nenvs.push_back(env_map.at(inst.envs[i]));
                        else
                            nenvs.push_back(inst.envs[i]);
                        lmb_env_map[env_id(i)] = env_id(j++);
                    }
                }
                inst.envs = nenvs;
                inst.lmb->env_cnt = nenvs.size();

                // it's static!! make an alias
                if (nenvs.empty()) {
                    alias[inst.retv] = inst.lmb;
                    assert(inst.lmb->obj.type == code_id_t::NONE);
                    inst.lmb->obj = next_global_id();
                } else {
                    ninsts.push_back(inst);
                }

                continue;
            }

            code_id_t orgi_func = inst.func;
            if (alias.count(inst.func))
                inst.func = alias[inst.func]->obj;
            else if (env_map.count(inst.func))
                inst.func = env_map.at(inst.func);

            // we call a temp lmb, the arg may also be an alias
            if (lmb_aliases.count(orgi_func)) {
                if (alias.count(inst.arg)) {
                    lmb_aliases[orgi_func][arg_id()] = alias[inst.arg];
                    inst.arg = alias[inst.arg]->obj;
                } else if (env_map.count(inst.arg)) {
                    assert(false);
                    inst.arg = env_map.at(inst.arg);
                }
                ninsts.push_back(inst);
                continue;
            }

            if (alias.count(inst.arg))
                inst.arg = alias[inst.arg]->obj;
            else if (env_map.count(inst.arg))
                inst.arg = env_map.at(inst.arg);
            ninsts.push_back(inst);
        }

        for (auto &inst : lmb->body.insts)
            if (inst.type == code_inst_t::LAMBDA)
                __extract_static_lmb(inst.lmb, lmb_aliases[inst.retv], lmb_env_maps[inst.retv]);

        if (alias.count(lmb->body.retv))
            lmb->body.retv = alias[lmb->body.retv]->obj;

        lmb->body.insts = ninsts;
        for (auto &pair : alias)
            lmb->body.deps.insert(pair.second);
    }

    void __inline_temp_lmb(std::shared_ptr<code_lmb_t> lmb) {

        bool run = true;

        while (run) {
            run = false;

            std::map<code_id_t, std::shared_ptr<code_lmb_t>> local_lmbs;
            std::map<code_id_t, std::map<code_id_t, code_id_t>> env_maps;
            std::set<code_id_t> local_func_lmbs;
            std::map<code_id_t, int> local_used;

            for (auto &inst : lmb->body.insts) {
                if (inst.type == code_inst_t::LAMBDA) {
                    local_lmbs[inst.retv] = inst.lmb;
                    __inline_temp_lmb(inst.lmb);
                    auto &env_map = env_maps[inst.retv];
                    for (int i = 0; i < (int)inst.envs.size(); i++) {
                        local_used[inst.envs[i]]++;
                        env_map[env_id(i)] = inst.envs[i];
                    }
                } else {
                    local_used[inst.func]++;
                    local_used[inst.arg]++;
                    if (local_lmbs.count(inst.func))
                        local_func_lmbs.insert(inst.func);
                }
            }

            std::vector<code_inst_t> ninsts;
            for (auto &inst : lmb->body.insts) {

                if (inst.type == code_inst_t::LAMBDA) {
                    if (!local_func_lmbs.count(inst.retv) || local_used[inst.retv] != 1)
                        ninsts.push_back(inst);
                    continue;
                }

                if (!local_func_lmbs.count(inst.func) || local_used[inst.func] != 1) {
                    ninsts.push_back(inst);
                    continue;
                }

                run = true;

                std::shared_ptr<code_lmb_t> ilmb = local_lmbs[inst.func];
                std::map<code_id_t, code_id_t> local_id_map;

                local_id_map[ilmb->body.retv] = inst.retv;
                local_id_map[arg_id()] = inst.arg;
                for (int i = 0; i < (int)inst.envs.size(); i++)
                    local_id_map[env_id(i)] = inst.envs[i];
                for (auto &pair : env_maps[inst.func])
                    local_id_map.insert(pair);

                // inline!!
                for (auto &iinst : ilmb->body.insts) {

                    if (!local_id_map.count(iinst.retv))
                        local_id_map[iinst.retv] = local_id(lmb->next_local_id++);
                    iinst.retv = local_id_map[iinst.retv];

                    if (iinst.type == code_inst_t::LAMBDA) {
                        for (auto &env : iinst.envs)
                            if (local_id_map.count(env))
                                env = local_id_map[env];
                    } else {
                        if (local_id_map.count(iinst.func))
                            iinst.func = local_id_map[iinst.func];
                        if (local_id_map.count(iinst.arg))
                            iinst.arg = local_id_map[iinst.arg];
                    }

                    ninsts.push_back(iinst);
                }

                // alter deps
                lmb->body.deps.erase(ilmb);
                lmb->body.deps.insert(ilmb->body.deps.begin(), ilmb->body.deps.end());
            }

            lmb->body.insts = ninsts;
        }
    }

    void __inline_static_lmb(std::shared_ptr<code_lmb_t> lmb) {
    }
};


transpiler_t::transpiler_t(std::set<std::string> &_builtins)
    : builtins(_builtins), impl(new impl_t(this)) {}

void transpiler_t::transpile(node_hdr_t node, std::ostream &stm) {
    impl->transpile(node, stm);
}
