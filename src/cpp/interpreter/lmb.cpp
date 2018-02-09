#include <fstream>
#include <sstream>
#include <iostream>
#include <deque>
#include <vector>
#include <map>
#include <utility>
#include <tuple>
#include <memory>
#include <cctype>
#include <cassert>

using namespace std;

// shptr_t {{{

template <typename T>
struct shptr_t {

    T *ptr;

    shptr_t() : ptr(nullptr) {}
    shptr_t(T *_ptr) : ptr(_ptr) {
        if (ptr)
            ++ptr->ref_cnt;
    }
    shptr_t(shptr_t &&ref) : ptr(ref.ptr) {
        ref.ptr = 0x0;
    }
    shptr_t(const shptr_t &ref) : shptr_t(ref.ptr) {}

    shptr_t& operator=(const shptr_t &ref) {
        if (ptr && --ptr->ref_cnt == 0)
            delete ptr;
        if ((ptr = ref.ptr))
            ++ptr->ref_cnt;
        return *this;
    }

    ~shptr_t() {
        if (ptr && --ptr->ref_cnt == 0)
            delete ptr;
    }

    T *operator->() const { return ptr; }
    T& operator*() const { return *ptr; }

    bool operator==(const shptr_t &obj) const {
        return ptr == obj.ptr;
    }
};

// }}}

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
        table[idx] = move(make_pair(hash_val, make_unique<pair<K, V>>(forward<KU>(k), V())));
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

// }}}

struct lmb_t;
using lmb_idx_t = unsigned long;
using lmb_hdr_t = shptr_t<const lmb_t>;

template <typename... Args>
lmb_hdr_t make_lmb(Args&&... args) {
    return shptr_t<const lmb_t>(new lmb_t(forward<Args>(args)...));
}

using env_t = vector<lmb_hdr_t>;
using env_idx_t = vector<lmb_idx_t>;

struct shadow_env_t {
    const lmb_hdr_t &shadow_val;
    const env_t &orgi_env;
    const lmb_hdr_t& operator[](int idx) const {
        return idx == 0 ? shadow_val : orgi_env[idx-1];
    }
};

// TODO: better hash
struct env_hash_t {
    size_t operator()(const env_idx_t &ienv) const {
        auto hasher = hash<lmb_idx_t>();
        size_t retv = 0;
        for (auto &v : ienv)
            retv = retv * 17 + hasher(v);
        return retv;
    }
};

struct expr_t {
    mutable hash_map_t<env_idx_t, lmb_hdr_t, env_hash_t> cache;
    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const = 0;
    virtual ~expr_t() {};
};

struct lmb_t {

    static lmb_idx_t gidx;

    const expr_t *body;
    const env_t env;
    const lmb_idx_t idx;
    mutable unsigned long ref_cnt;
    mutable hash_map_t<lmb_idx_t, lmb_hdr_t> cache;

    lmb_t(const expr_t *_body, const env_t &_env) :
        body(_body), env(_env), ref_cnt(0), idx(gidx++) {}
    lmb_t(const expr_t *_body, env_t &&_env) :
        body(_body), env(move(_env)), ref_cnt(0), idx(gidx++) {}
};
lmb_idx_t lmb_t::gidx = 0;

struct lmb_expr_t : public expr_t {

    const expr_t *body;
    const vector<size_t> arg_map;

    lmb_expr_t(const expr_t *_body, const vector<size_t> &_arg_map) :
        body(_body), arg_map(_arg_map) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {

        env_idx_t ienv;
        ienv.reserve(arg_map.size());
        for (auto idx : arg_map)
            ienv.emplace_back(env[idx]->idx);

        auto &ref = body->cache[move(ienv)];
        if (ref == nullptr) {

            env_t nenv;
            nenv.reserve(arg_map.size());
            for (auto idx : arg_map)
                nenv.emplace_back(env[idx]);

            ref = make_lmb(body, move(nenv));
        }

        return ref;
    }
};

struct apply_expr_t : public expr_t {

    const expr_t* func;
    const expr_t* arg;

    apply_expr_t(const expr_t *_func, const expr_t *_arg) :
        func(_func), arg(_arg) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        auto& lfunc = func->eval(env);
        auto& larg = arg->eval(env);
        auto& ref = lfunc->cache[larg->idx];
        if (ref == nullptr)
            ref = lfunc->body->eval(shadow_env_t{larg, lfunc->env});
        return ref;
    }
};

struct ref_expr_t : public expr_t {

    const size_t ref_idx;

    ref_expr_t(size_t _ref_idx) : ref_idx(_ref_idx) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        return env[ref_idx];
    }
};

// parser {{{

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

struct parser_t {

    map<tuple<size_t>, const expr_t*> cache_ref;
    map<tuple<vector<size_t>, const expr_t*>, const expr_t*> cache_lmb;
    map<tuple<const expr_t*, const expr_t*>, const expr_t*> cache_apply;

    parser_t() {}

    const expr_t *parse_single_expr(tokenizer_t &tok, map<string, size_t> &ref) {

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
            auto &ent = cache_ref[make_tuple(ref[token])];
            if (ent == nullptr)
                ent = new ref_expr_t(ref[token]);
            return ent;
        }

        // lambda

        map<string, size_t> nref;
        string arg = tok.pop();
        assert(arg != "(" && arg != ")" && arg != "\\");
        nref[arg] = 0;
        const expr_t *body = parse_expr(tok, nref);

        nref.erase(arg);
        vector<size_t> arg_map(nref.size());
        for (auto pair : nref) {
            if (!ref.count(pair.first))
                ref.insert(make_pair(pair.first, ref.size()));
            arg_map[pair.second-1] = ref[pair.first];
        }

        auto &ent = cache_lmb[make_tuple(arg_map, body)];
        if (ent == nullptr)
            ent = new lmb_expr_t(body, arg_map);
        return ent;
    }

    const expr_t *parse_expr(tokenizer_t &tok, map<string, size_t> &ref) {

        const expr_t *func = parse_single_expr(tok, ref);
        while (tok.peak() != ")") {
            assert(tok.peak() != "");
            auto arg = parse_single_expr(tok, ref);
            auto &ent = cache_apply[make_tuple(func, arg)];
            if (ent == nullptr)
                ent = new apply_expr_t(func, arg);
            func = ent;
        }

        return func;
    }

    bool run_once(tokenizer_t &tok, map<string, lmb_hdr_t> &env) {

        if (tok.peak() == "")
            return false;

        map<string, size_t> ref;
        ref[""] = 0; // nullptr arg
        const expr_t *prog = parse_single_expr(tok, ref);
        ref.erase("");

        env_t nenv(ref.size());
        for (auto pair : ref) {
            if (!env.count(pair.first))
                std::cerr << pair.first << std::endl;
            assert(env.count(pair.first));
            nenv[pair.second-1] = env[pair.first];
        }

        prog->eval(shadow_env_t{nullptr, nenv});
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

    // FIXME: how about eof?

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


struct builtin_p0_expr_t : public expr_t {
    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        output(0);
        return env[0];
    }
};

struct builtin_p1_expr_t : public expr_t {
    virtual const lmb_hdr_t& eval(const shadow_env_t &env) const {
        output(1);
        return env[0];
    }
};

struct builtin_g_expr_t : public expr_t {
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
    env["__builtin_p0"] = make_lmb(new builtin_p0_expr_t(), env_t{});
    env["__builtin_p1"] = make_lmb(new builtin_p1_expr_t(), env_t{});
    env["__builtin_g"] = make_lmb(new lmb_expr_t(new lmb_expr_t(new lmb_expr_t(new builtin_g_expr_t(), {1, 2, 0}), {1, 0}), {0}), env_t{});

    while (parser.run_once(toks, env));
}

// }}}
