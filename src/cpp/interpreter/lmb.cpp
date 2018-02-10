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
using lmb_hdr_t = shared_ptr<const lmb_t>;

template <typename... Args>
lmb_hdr_t make_lmb(Args&&... args) {
    return make_shared<const lmb_t>(forward<Args>(args)...);
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
    const size_t cache_size;
    mutable hash_map_t<env_idx_t, lmb_hdr_t, env_hash_t> cache;
    expr_t(size_t _cache_size) : cache_size(_cache_size) {}
    virtual const lmb_hdr_t& eval(const shadow_env_t &env, vector<lmb_hdr_t>::iterator &cache_ptr) const = 0;
    virtual ~expr_t() {};
};

struct lmb_t {

    static lmb_idx_t gidx;

    const expr_t *body;
    const env_t env;
    const lmb_idx_t idx;
    mutable hash_map_t<lmb_idx_t, lmb_hdr_t> cache;
    mutable vector<lmb_hdr_t> expr_cache;

    template <typename EU>
    lmb_t(const expr_t *_body, EU&& _env) :
        body(_body), env(forward<EU>(_env)), idx(gidx++), expr_cache(body->cache_size) {}
};
lmb_idx_t lmb_t::gidx = 0;

template <bool arg_free>
struct lmb_expr_t : public expr_t {

    const expr_t *body;
    const vector<size_t> arg_map;

    lmb_expr_t(const expr_t *_body, const vector<size_t> &_arg_map) :
        expr_t(arg_free ? 1 : 0), body(_body), arg_map(_arg_map) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env, vector<lmb_hdr_t>::iterator &cache_ptr) const {

        auto ptr = cache_ptr;

        if (arg_free) {
            cache_ptr++;
            if (*ptr)
                return *ptr;
        }

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

        if (arg_free)
            *ptr = ref;
        return ref;
    }
};

template <bool arg_free>
struct apply_expr_t : public expr_t {

    const expr_t* func;
    const expr_t* arg;

    apply_expr_t(const expr_t *_func, const expr_t *_arg) :
        expr_t(_func->cache_size + _arg->cache_size + 1), func(_func), arg(_arg) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env, vector<lmb_hdr_t>::iterator &cache_ptr) const {

        auto ptr = cache_ptr;

        if (arg_free) {
            cache_ptr++;
            if (*ptr)
                return *ptr;
        }

        auto& lfunc = func->eval(env, cache_ptr);
        auto& larg = arg->eval(env, cache_ptr);
        auto& ref = lfunc->cache[larg->idx];
        if (ref == nullptr) {
            vector<lmb_hdr_t>::iterator ncache_ptr = lfunc->expr_cache.begin();
            ref = lfunc->body->eval(shadow_env_t{larg, lfunc->env}, ncache_ptr);
        }

        if (arg_free)
            *ptr = ref;
        return ref;
    }
};

struct ref_expr_t : public expr_t {

    const size_t ref_idx;

    ref_expr_t(size_t _ref_idx) : expr_t(0), ref_idx(_ref_idx) {}

    virtual const lmb_hdr_t& eval(const shadow_env_t &env, vector<lmb_hdr_t>::iterator &cache_ptr) const {
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

    const expr_t *parse_single_expr(tokenizer_t &tok, map<string, size_t> &ref, bool &arg_free) {

        string token = tok.pop();
        assert(token != "");

        if (token == "(") {
            bool narg_free = true;
            auto retv = parse_expr(tok, ref, narg_free);
            assert(tok.peak() == ")");
            tok.pop();
            arg_free &= narg_free;
            return retv;
        }
        if (token != "\\") {
            if (!ref.count(token))
                ref.insert(make_pair(token, ref.size()));
            auto &ent = cache_ref[make_tuple(ref[token])];
            if (ent == nullptr)
                ent = new ref_expr_t(ref[token]);
            arg_free &= ref[token] != 0;
            return ent;
        }

        // lambda

        bool narg_free = true;
        map<string, size_t> nref;
        string arg = tok.pop();
        assert(arg != "(" && arg != ")" && arg != "\\");
        nref[arg] = 0;
        const expr_t *body = parse_expr(tok, nref, narg_free);

        nref.erase(arg);
        bool sarg_free = true;
        vector<size_t> arg_map(nref.size());
        for (auto pair : nref) {
            if (!ref.count(pair.first))
                ref.insert(make_pair(pair.first, ref.size()));
            arg_map[pair.second-1] = ref[pair.first];
            sarg_free &= ref[pair.first] != 0;
        }

        auto &ent = cache_lmb[make_tuple(arg_map, body)];
        if (ent == nullptr) {
            if (sarg_free)
                ent = new lmb_expr_t<true>(body, arg_map);
            else
                ent = new lmb_expr_t<false>(body, arg_map);
        }
        arg_free &= sarg_free;
        return ent;
    }

    const expr_t *parse_expr(tokenizer_t &tok, map<string, size_t> &ref, bool &arg_free) {

        const expr_t *func = parse_single_expr(tok, ref, arg_free);
        while (tok.peak() != ")") {
            assert(tok.peak() != "");
            auto arg = parse_single_expr(tok, ref, arg_free);
            auto &ent = cache_apply[make_tuple(func, arg)];
            if (ent == nullptr) {
                if (arg_free)
                    ent = new apply_expr_t<true>(func, arg);
                else
                    ent = new apply_expr_t<false>(func, arg);
            }
            func = ent;
        }

        return func;
    }

    bool run_once(tokenizer_t &tok, map<string, lmb_hdr_t> &env) {

        if (tok.peak() == "")
            return false;

        bool arg_free = true;
        map<string, size_t> ref;
        ref[""] = 0; // nullptr arg
        const expr_t *prog = parse_single_expr(tok, ref, arg_free);
        assert(arg_free);
        ref.erase("");

        env_t nenv(ref.size());
        for (auto pair : ref) {
            if (!env.count(pair.first))
                std::cerr << pair.first << std::endl;
            assert(env.count(pair.first));
            nenv[pair.second-1] = env[pair.first];
        }

        vector<lmb_hdr_t> expr_cache(prog->cache_size);
        vector<lmb_hdr_t>::iterator cache_ptr = expr_cache.begin();
        prog->eval(shadow_env_t{nullptr, nenv}, cache_ptr);
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
    builtin_p0_expr_t() : expr_t(0) {}
    virtual const lmb_hdr_t& eval(const shadow_env_t &env, vector<lmb_hdr_t>::iterator &cache_ptr) const {
        output(0);
        return env[0];
    }
};

struct builtin_p1_expr_t : public expr_t {
    builtin_p1_expr_t() : expr_t(0) {}
    virtual const lmb_hdr_t& eval(const shadow_env_t &env, vector<lmb_hdr_t>::iterator &cache_ptr) const {
        output(1);
        return env[0];
    }
};

struct builtin_g_expr_t : public expr_t {
    builtin_g_expr_t() : expr_t(0) {}
    virtual const lmb_hdr_t& eval(const shadow_env_t &env, vector<lmb_hdr_t>::iterator &cache_ptr) const {
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
    env["__builtin_g"] = make_lmb(new lmb_expr_t<false>(new lmb_expr_t<false>(new lmb_expr_t<false>(new builtin_g_expr_t(), {1, 2, 0}), {1, 0}), {0}), env_t{});

    while (parser.run_once(toks, env));
}

// }}}
