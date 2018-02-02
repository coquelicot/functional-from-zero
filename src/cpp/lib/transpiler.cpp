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
                return stm << "_e[" << id.val << "]";
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
    int env_cnt;
    code_block_t body;
    int next_local_id;
};

struct transpiler_t::impl_t {

    int global_id;
    std::map<std::string, code_id_t> builtins;
    std::set<code_id_t> builtin_ids;

    impl_t(transpiler_t *parent) : global_id(0) {
        for (auto str : parent->builtins)
            builtins.insert(std::make_pair(str, next_global_id()));
        for (auto pair : builtins)
            builtin_ids.insert(pair.second);
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

        global_id = builtins.size();
        std::map<std::string, int> ident_cnt;
        std::vector<code_inst_t> insts;
        std::map<std::string, code_id_t> envs;
        std::set<std::shared_ptr<code_lmb_t>> deps;

        code_id_t name = next_global_id();
        code_id_t retv = local_id(0);
        int next_local_id = 1;
        int next_env_id = 0;
        __transpile(node, next_local_id, next_env_id, retv, insts, envs, deps, ident_cnt);

        code_block_t block{retv, insts, deps};
        std::shared_ptr<code_lmb_t> prog(new code_lmb_t{name, 0, block, next_local_id});

        // optimize
        __inline_temp_lmb(prog);
        __extract_static_lmb(prog); // this assumes that all temp lmb are inlined
        __dedup_lmb(prog);

        // output
        __emit(prog, stm);
    }

    void __emit(std::shared_ptr<code_lmb_t> lmb, std::ostream &stm) {

        stm << "#include \"runtime.hpp\"\n";
        for (auto pair : builtins)
            stm << "auto& " << pair.second << " = " << pair.first << ";\n";

        std::set<std::shared_ptr<code_lmb_t>> emited;
        __emit(lmb, stm, emited);

        stm << "int main() {\n"
            << "  " << lmb->name << "->exec(0x0, 0x0);\n"
            << "}\n";
    }

    void __emit(std::shared_ptr<code_lmb_t> lmb, std::ostream &stm, std::set<std::shared_ptr<code_lmb_t>> &emited) {

        if (emited.count(lmb))
            return;
        emited.insert(lmb);

        for (auto dep : lmb->body.deps)
            __emit(dep, stm, emited);

        // FIXME: hard coded var name
        stm << "lmb_t *" << lmb->name << "_t(lmb_t *_e[], lmb_t *" << arg_id() << ") {\n";
        for (auto inst : lmb->body.insts) {
            stm << "    lmb_t *" << inst.retv << " = ";
            if (inst.type == code_inst_t::APPLY) {
                stm << "apply(" << inst.func << ", " << inst.arg << ");\n";
            } else {
                stm << "create<" << inst.lmb->name.val << ", " << inst.envs.size() << ">({{"
                    << "(long)" << inst.lmb->name << "_t, 1";
                for (auto it = inst.envs.begin(); it != inst.envs.end(); it++)
                    stm << ", (long)" << *it;
                stm << ", 0}});\n";
            }
        }
        stm << "    return " << lmb->body.retv << ";\n";
        stm << "};\n";

        // static obj
        if (lmb->env_cnt == 0)
            stm << "lmb_t *" << lmb->name << "(create<" << lmb->name.val << ", 0>({{(long)" << lmb->name << "_t, 1, 0}}));\n";
    }

    void __transpile(
        node_hdr_t _node,
        int &next_local_id,
        int &next_env_id,
        code_id_t &retv,
        std::vector<code_inst_t> &insts,
        std::map<std::string, code_id_t> &envs,
        std::set<std::shared_ptr<code_lmb_t>> &deps,
        std::map<std::string, int> &ident_cnt
    ) {

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

            code_id_t func = local_id(next_local_id++);
            code_id_t arg = local_id(next_local_id++);
            __transpile(node.nd_fun, next_local_id, next_env_id, func, insts, envs, deps, ident_cnt);
            __transpile(node.nd_arg, next_local_id, next_env_id, arg, insts, envs, deps, ident_cnt);
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
            __transpile(node.nd_body, lmb_next_local_id, lmb_next_env_id, lmb_retv, lmb_insts, lmb_envs, lmb_deps, ident_cnt);
            ident_cnt[node.arg]--;
            lmb_envs.erase(node.arg);

            code_block_t block{lmb_retv, lmb_insts, lmb_deps};
            std::shared_ptr<code_lmb_t> lmb(new code_lmb_t{name, (int)lmb_envs.size(), block, lmb_next_local_id});
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

        for (auto &inst : lmb->body.insts) {

            if (inst.type == code_inst_t::LAMBDA) {

                // clean up params & build alias
                std::vector<code_id_t> nenvs;
                std::map<code_id_t, code_id_t> lmb_env_map;
                std::map<code_id_t, std::shared_ptr<code_lmb_t>> lmb_alias;
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
                if (nenvs.empty())
                    alias[inst.retv] = inst.lmb;
                else
                    ninsts.push_back(inst);

                // extract recursively no matter what
                __extract_static_lmb(inst.lmb, lmb_alias, lmb_env_map);

                continue;
            }

            if (alias.count(inst.func))
                inst.func = alias[inst.func]->name;
            else if (env_map.count(inst.func))
                inst.func = env_map.at(inst.func);
            if (alias.count(inst.arg))
                inst.arg = alias[inst.arg]->name;
            else if (env_map.count(inst.arg))
                inst.arg = env_map.at(inst.arg);
            ninsts.push_back(inst);
        }

        lmb->body.insts = ninsts;
        if (alias.count(lmb->body.retv))
            lmb->body.retv = alias[lmb->body.retv]->name;
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

    typedef std::pair<std::shared_ptr<code_lmb_t>, std::vector<int>> code_lmb_ref_t;
    typedef std::pair<std::string, std::vector<int>> code_lmb_sig_t;

    std::string __calc_lmb_sig(std::shared_ptr<code_lmb_t> lmb) {

        std::stringstream ss;

        for (auto &inst : lmb->body.insts) {
            if (inst.type == code_inst_t::APPLY) {
                ss << "A" << inst.retv << inst.func << inst.arg;
            } else {
                ss << "L" << inst.retv << inst.lmb->name;
                for (auto env : inst.envs)
                    ss << env;
            }
        }
        ss << "R" << lmb->body.retv;

        return ss.str();
    }

    std::vector<int> __relabel_lmb(std::shared_ptr<code_lmb_t> lmb, std::map<code_id_t, code_lmb_ref_t> &lmb_remap) {

        std::vector<int> env_map;
        std::map<code_id_t, code_id_t> ident_map;
        int next_local_id = 0, next_env_id = 0;

        lmb->body.deps.clear();

        auto remap_id = [&](code_id_t id) {
            switch (id.type) {
                case code_id_t::ARG:
                    return id;
                case code_id_t::GLOBAL:
                    if (builtin_ids.count(id))
                        return id;
                    lmb->body.deps.insert(lmb_remap[id].first);
                    return lmb_remap[id].first->name;
                case code_id_t::LOCAL:
                    if (!ident_map.count(id))
                        return ident_map[id] = local_id(next_local_id++);
                    else
                        return ident_map[id];
                case code_id_t::ENV:
                    if (!ident_map.count(id)) {
                        env_map.push_back(id.val);
                        return ident_map[id] = env_id(next_env_id++);
                    } else {
                        return ident_map[id];
                    }
                default:
                    assert(false);
            }
        };

        for (auto &inst : lmb->body.insts) {
            inst.retv = remap_id(inst.retv);
            if (inst.type == code_inst_t::APPLY) {
                inst.func = remap_id(inst.func);
                inst.arg = remap_id(inst.arg);
            } else {
                auto &pair = lmb_remap[inst.lmb->name];
                std::vector<code_id_t> nenvs;
                for (auto idx : pair.second)
                    nenvs.push_back(remap_id(inst.envs[idx]));
                inst.lmb = pair.first;
                inst.envs = nenvs;
                lmb->body.deps.insert(inst.lmb);
            }
        }
        lmb->body.retv = remap_id(lmb->body.retv);

        assert((int)env_map.size() == lmb->env_cnt);
        return env_map;
    }

    void __do_dedup_lmb(
        std::shared_ptr<code_lmb_t> lmb,
        std::map<std::string, std::shared_ptr<code_lmb_t>> &sig_to_lmb,
        std::map<code_id_t, code_lmb_ref_t> &lmb_remap
    ) {
        for (auto dep : lmb->body.deps)
            if (!lmb_remap.count(dep->name))
                __do_dedup_lmb(dep, sig_to_lmb, lmb_remap);

        // do remap
        std::vector<int> env_ord = __relabel_lmb(lmb, lmb_remap);
        std::string lmb_sig = __calc_lmb_sig(lmb);
        auto it = sig_to_lmb.find(lmb_sig);

        if (it != sig_to_lmb.end()) {
            std::vector<int> env_map(lmb->env_cnt);
            lmb_remap[lmb->name] = code_lmb_ref_t{it->second, env_ord};
        } else {
            std::vector<int> rev_map(lmb->env_cnt);
            for (int i = 0; i < lmb->env_cnt; i++)
                rev_map[env_ord[i]] = i;
            sig_to_lmb[lmb_sig] = lmb;
            lmb_remap[lmb->name] = code_lmb_ref_t{lmb, env_ord};
        }
    }

    void __dedup_lmb(std::shared_ptr<code_lmb_t> lmb) {
        std::map<std::string, std::shared_ptr<code_lmb_t>> sig_to_lmb;
        std::map<code_id_t, code_lmb_ref_t> lmb_remap;
        __do_dedup_lmb(lmb, sig_to_lmb, lmb_remap);
    }
};


transpiler_t::transpiler_t(std::set<std::string> &_builtins)
    : builtins(_builtins), impl(new impl_t(this)) {}

void transpiler_t::transpile(node_hdr_t node, std::ostream &stm) {
    impl->transpile(node, stm);
}
