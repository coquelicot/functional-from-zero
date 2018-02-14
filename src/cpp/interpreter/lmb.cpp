#include <fstream>
#include <sstream>
#include <iostream>
#include <deque>
#include <map>
#include <tuple>
#include <utility>
#include <vector>
#include <memory>
#include <cctype>
#include <cassert>

using namespace std;

// hash_map_t {{{

template <typename K, typename V, typename H=hash<K>>
struct hash_map_t {

    H hasher;
    size_t size;
    vector<pair<size_t, unique_ptr<pair<K, V>>>> table;

    hash_map_t() : size(0), table(1) {}

    template <class KU>
    V& operator[](KU&& k) {

        size_t idx;
        const size_t hash_val = hasher(k);
        const size_t mask = table.size() - 1;

        // exist?
        for (idx = hash_val & mask; table[idx].second; idx = (idx + 1) & mask)
            if (table[idx].first == hash_val && table[idx].second->first == k)
                return table[idx].second->second;

        // insert
        ++size;
        table[idx] = move(make_pair(hash_val, unique_ptr<pair<K, V>>(new pair<K, V>(forward<KU>(k), V()))));
        V& retv = table[idx].second->second;

        // extend if load >= 2/3
        if (size * 3 >= table.size() * 2)
            _extend();

        return retv;
    }

    void _extend() {

        decltype(table) ntable(table.size() * 2);
        const size_t nmask = ntable.size() - 1;

        for (auto &ent : table)
            if (ent.second) {
                size_t idx = ent.first & nmask;
                while (ntable[idx].second)
                    idx = (idx + 1) & nmask;
                ntable[idx] = move(ent);
            }

        table.swap(ntable);
    }
};

namespace std {

    constexpr static size_t _combine(size_t a, size_t b) {
        return a * 17 + b;
    }

    template <typename U, typename V>
    struct hash<pair<U, V>> {
        hash<U> uhash;
        hash<V> vhash;
        size_t operator()(const pair<U, V> &p) const {
            return _combine(uhash(p.first), vhash(p.second));
        }
    };

    template <typename T, size_t idx=tuple_size<T>::value>
    struct _tuple_hash {
        _tuple_hash<T, idx-1> uhash;
        hash<typename tuple_element<idx-1, T>::type> vhash;
        size_t operator()(const T& t) const {
            return _combine(uhash(t), vhash(get<idx-1>(t)));
        }
    };
    template <typename T>
    struct _tuple_hash<T, 0> {
        size_t operator()(const T& t) const { return 0; }
    };
    template <typename... Args>
    struct hash<tuple<Args...>> : _tuple_hash<tuple<Args...>> {};

    template <typename V>
    struct hash<vector<V>> {
        hash<V> vhash;
        size_t operator()(const vector<V> &vs) const {
            size_t retv = 0;
            for (auto &v : vs)
                retv = _combine(retv, vhash(v));
            return retv;
        }
    };
}

// }}}

// lmb_t env_t expr_t {{{

struct lmb_t;
using lmb_idx_t = unsigned long;
using lmb_hdr_t = shared_ptr<const lmb_t>;
using arg_map_t = vector<size_t>;

using env_t = vector<lmb_hdr_t>;
using env_idx_t = vector<lmb_idx_t>;

struct shadow_env_t {
    const lmb_hdr_t &shadow_val;
    const env_t &orgi_env;
    const lmb_hdr_t& operator[](int idx) const {
        return idx == 0 ? shadow_val : orgi_env[idx-1];
    }
};

struct expr_t;
using expr_hdr_t = shared_ptr<const expr_t>;
struct expr_t {
    mutable hash_map_t<env_idx_t, lmb_hdr_t> lmb_cache;
    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const = 0;
    virtual const expr_hdr_t const_fold(size_t idx, const lmb_hdr_t &val) const {
        assert(false);
    }
    virtual const expr_hdr_t remap_idx(const arg_map_t &arg_map) const {
        assert(false);
    }
    virtual ~expr_t() {};
};

template <typename T, typename... Args>
struct cached_expr_t : public expr_t {

    using key_t = tuple<Args...>;
    static hash_map_t<key_t, expr_hdr_t> expr_cache;

    static expr_hdr_t create(const Args&... args) {
        auto &ref = expr_cache[key_t(args...)];
        if (ref == nullptr)
            return ref = make_shared<T>(args...);
        return ref;
    }
};
template <typename T, typename... Args>
hash_map_t<typename cached_expr_t<T, Args...>::key_t, expr_hdr_t> cached_expr_t<T, Args...>::expr_cache;

struct lmb_t {

    static lmb_idx_t gidx;

    const expr_hdr_t body;
    const env_t env;
    const lmb_idx_t idx;
    mutable hash_map_t<lmb_idx_t, lmb_hdr_t> exec_cache;

    template <typename EU>
    lmb_t(const expr_hdr_t& _body, EU&& _env) :
        body(_body), env(forward<EU>(_env)), idx(gidx++) {}

    const lmb_hdr_t& exec(const lmb_hdr_t &arg) const {
        auto& ref = exec_cache[arg->idx];
        if (ref == nullptr)
            ref = body->eval(shadow_env_t{arg, env});
        return ref;
    }
};
lmb_idx_t lmb_t::gidx = 0;

template <typename... Args>
lmb_hdr_t make_lmb(Args&&... args) {
    return make_shared<const lmb_t>(forward<Args>(args)...);
}

// }}}

// X_expr_t {{{

struct const_expr_t : public cached_expr_t<const_expr_t, lmb_hdr_t> {

    const lmb_hdr_t val;

    const_expr_t(const lmb_hdr_t &_val) :
        val(_val) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        return val;
    }

    virtual const expr_hdr_t const_fold(size_t idx, const lmb_hdr_t &_val) const {
        return const_expr_t::create(val);
    }

    virtual const expr_hdr_t remap_idx(const arg_map_t &arg_map) const {
        return const_expr_t::create(val);
    }
};

struct lmb_expr_t : public cached_expr_t<lmb_expr_t, expr_hdr_t, arg_map_t> {

    const expr_hdr_t body;
    const arg_map_t arg_map;

    lmb_expr_t(const expr_hdr_t &_body, const arg_map_t &_arg_map) :
        body(_body), arg_map(_arg_map) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {

        env_idx_t ienv;
        ienv.reserve(arg_map.size());
        for (auto idx : arg_map)
            ienv.emplace_back(env[idx]->idx);

        auto &ref = body->lmb_cache[move(ienv)];
        if (ref == nullptr) {

            env_t nenv;
            nenv.reserve(arg_map.size());
            for (auto idx : arg_map)
                nenv.emplace_back(env[idx]);

            ref = make_lmb(body, move(nenv));
        }

        return ref;
    }

    virtual const expr_hdr_t const_fold(size_t idx, const lmb_hdr_t &val) const {

        ssize_t skip = -1;
        arg_map_t narg_map;
        narg_map.reserve(arg_map.size());

        for (size_t i = 0; i < arg_map.size(); i++)
            if (arg_map[i] == idx) {
                assert(skip == -1);
                skip = i;
            } else {
                narg_map.push_back(arg_map[i] - (idx && arg_map[i] > idx ? 1 : 0));
            }

        auto nbody = skip == -1 ? body : body->const_fold(skip + 1, val);
        if (narg_map.size() > 0)
            return lmb_expr_t::create(nbody, narg_map);
        else {
            //cerr << "const lmb" << endl;
            return const_expr_t::create(make_lmb(nbody, env_t{}));
        }
    }

    virtual const expr_hdr_t remap_idx(const arg_map_t &ref_arg_map) const {

        arg_map_t narg_map;
        narg_map.reserve(arg_map.size());
        for (auto idx : arg_map) {
            assert(idx != 0);
            narg_map.push_back(ref_arg_map[idx - 1]);
        }

        return lmb_expr_t::create(body, narg_map);
    }
};

struct apply_expr_t : public cached_expr_t<apply_expr_t, expr_hdr_t, expr_hdr_t> {

    const expr_hdr_t func;
    const expr_hdr_t arg;

    apply_expr_t(const expr_hdr_t &_func, const expr_hdr_t &_arg) :
        func(_func), arg(_arg) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        auto& lfunc = func->eval(env);
        auto& larg = arg->eval(env);
        return lfunc->exec(larg);
    }

    virtual const expr_hdr_t const_fold(size_t idx, const lmb_hdr_t &val) const {
        return _const_fold(func->const_fold(idx, val), arg->const_fold(idx, val));
    }

    static expr_hdr_t _const_fold(const expr_hdr_t &func, const expr_hdr_t &arg) {
        try {
            const const_expr_t &carg = dynamic_cast<const const_expr_t&>(*arg);
            try {
                const const_expr_t &cfunc = dynamic_cast<const const_expr_t&>(*func);
                //cerr << "const apply" << endl;
                return const_expr_t::create(cfunc.val->exec(carg.val));
            } catch (bad_cast) {
                try {
                    const lmb_expr_t &lfunc = dynamic_cast<const lmb_expr_t&>(*func);
                    //cerr << "inline lmb" << endl;
                    return lfunc.body->const_fold(0, carg.val)->remap_idx(lfunc.arg_map);
                } catch (bad_cast) {}
            }
        } catch (bad_cast) {}
        return apply_expr_t::create(func, arg);
    }

    virtual const expr_hdr_t remap_idx(const arg_map_t &arg_map) const {
        return apply_expr_t::create(func->remap_idx(arg_map), arg->remap_idx(arg_map));
    }
};

struct ref_expr_t : public cached_expr_t<ref_expr_t, size_t> {

    const size_t ref_idx;

    ref_expr_t(size_t _ref_idx) : ref_idx(_ref_idx) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        return env[ref_idx];
    }

    virtual const expr_hdr_t const_fold(size_t idx, const lmb_hdr_t &val) const {
        if (idx == ref_idx) {
            //cerr << "const ref" << endl;
            return const_expr_t::create(val);
        } else
            return ref_expr_t::create(ref_idx - (idx && ref_idx > idx ? 1 : 0));
    }

    virtual const expr_hdr_t remap_idx(const arg_map_t &arg_map) const {
        assert(ref_idx != 0);
        return ref_expr_t::create(arg_map[ref_idx - 1]);
    }
};

// }}}

// tokenizer {{{

struct tokenizer_t {

    istream &stm;
    string spe_chars;
    deque<string> toks;

    tokenizer_t(istream &_stm) : stm(_stm), spe_chars("()\\") {}

    string peak() {
        while (toks.empty())
            if (!_read_more())
                return "";
        return toks.front();
    }

    string pop() {
        string retv = peak();
        toks.pop_front();
        return retv;
    }

    bool _read_more() {

        string line;
        do {
            if (!getline(stm, line))
                return false;
        } while (line.length() == 0 || _is_comment(line));

        _parse(line);
        return true;
    }

    bool _is_comment(std::string &str) {
        for (auto chr : str)
            if (!isspace(chr))
                return chr == '#';
        return false;
    }

    void _parse(string &line) {

        stringstream buf(line);

        while (true) {

            char c;
            do {
                if (!buf.get(c))
                    return;
            } while (isspace(c));

            if (spe_chars.find(c) != string::npos) {
                toks.push_back(string(1, c));
                continue;
            }

            // identity
            string tok;
            do {
                tok += c;
                if (!buf.get(c)) {
                    toks.push_back(tok);
                    return;
                }
            } while (!isspace(c) && spe_chars.find(c) == string::npos);

            toks.push_back(tok);
            buf.unget();
        }
    }

};

// }}}

// parser {{{

struct parser_t {

    parser_t() {}

    expr_hdr_t parse_single_expr(tokenizer_t &tok, map<string, size_t> &ref) {

        string token = tok.pop();
        assert(token != "");

        if (token == "(") {
            auto retv = parse_expr(tok, ref);
            assert(tok.peak() == ")");
            tok.pop();
            return retv;
        }
        if (token != "\\") {
            if (!ref.count(token))
                ref.insert(make_pair(token, ref.size()));
            return ref_expr_t::create(ref[token]);
        }

        // lambda

        map<string, size_t> nref;
        string arg = tok.pop();
        assert(arg != "(" && arg != ")" && arg != "\\");
        nref[arg] = 0;
        auto body = parse_expr(tok, nref);

        nref.erase(arg);
        arg_map_t arg_map(nref.size());
        for (auto pair : nref) {
            if (!ref.count(pair.first))
                ref.insert(make_pair(pair.first, ref.size()));
            arg_map[pair.second-1] = ref[pair.first];
        }

        return lmb_expr_t::create(body, arg_map);
    }

    expr_hdr_t parse_expr(tokenizer_t &tok, map<string, size_t> &ref) {

        auto func = parse_single_expr(tok, ref);
        while (tok.peak() != ")" && tok.peak() != "") {
            auto arg = parse_single_expr(tok, ref);
            func = apply_expr_t::create(func, arg);
        }

        return func;
    }

    bool run_once(tokenizer_t &tok, map<string, lmb_hdr_t> &env) {

        if (tok.peak() == "")
            return false;

        map<string, size_t> ref;
        auto prog = parse_single_expr(tok, ref);

        lmb_hdr_t arg = nullptr;
        env_t nenv(ref.empty() ? 0 : ref.size()-1);
        for (auto pair : ref) {
            if (!env.count(pair.first)) {
                std::cerr << "Unknown ident: " << pair.first << std::endl;
                return false;
            } else if (pair.second > 0) {
                nenv[pair.second-1] = env[pair.first];
            } else {
                arg = env[pair.first];
            }
        }

#define CONST_FOLD 1
#ifdef CONST_FOLD
        for (size_t i = 0; i < nenv.size(); i++)
            prog = prog->const_fold(1, nenv[i]);
        prog = prog->const_fold(0, arg);
        prog->eval(shadow_env_t{nullptr, env_t{}});
#else
        prog->eval(shadow_env_t{arg, nenv});
#endif
        return true;
    }
};

// }}}

// runtime {{{

void output(int bit) {

    static int pos = 7;
    static int val = 0;

    val |= (bit << pos--);
    if (pos < 0) {
        cout << char(val);
        cout.flush();
        pos = 7, val = 0;
    }
}

int input() {

    static int pos = -1;
    static int val = 0;

    if (pos < 0) {
        val = cin.get();
        if (val == EOF)
            return EOF;
        pos = 7;
    }

    return (val >> pos--) & 1;
}


struct builtin_p0_expr_t : public cached_expr_t<builtin_p0_expr_t> {
    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        output(0);
        return env[0];
    }
};

struct builtin_p1_expr_t : public cached_expr_t<builtin_p1_expr_t> {
    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        output(1);
        return env[0];
    }
};

struct builtin_g_expr_t : public cached_expr_t<builtin_g_expr_t> {
    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        int bit = input();
        return bit == EOF ? env[3] : env[bit+1];
    }
};

// }}}

// main {{{

int main(int argc, char *args[]) {

    assert(argc > 1);
    fstream fin(args[1]);

    tokenizer_t toks(fin);
    parser_t parser;

    map<string, lmb_hdr_t> env;
    env["__builtin_p0"] = make_lmb(builtin_p0_expr_t::create(), env_t{});
    env["__builtin_p1"] = make_lmb(builtin_p1_expr_t::create(), env_t{});
    env["__builtin_g"] = make_lmb(
        lmb_expr_t::create(
           lmb_expr_t::create(
               lmb_expr_t::create(
                    builtin_g_expr_t::create(),
                    arg_map_t{1, 2, 0}),
                arg_map_t{1, 0}),
            arg_map_t{0}),
        env_t{}
    );

    while (parser.run_once(toks, env));
}

// }}}
