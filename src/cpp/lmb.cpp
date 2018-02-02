#include <fstream>
#include <sstream>
#include <iostream>
#include <deque>
#include <vector>
#include <map>
#include <memory>
#include <utility>
#include <tuple>
#include <functional>
#include <cctype>
#include <cassert>

using namespace std;

struct lmb_t;

using env_t = vector<shared_ptr<const lmb_t>>;
struct shadow_env_t {

    const shared_ptr<const lmb_t> &shadow_val;
    const env_t &orgi_env;

    shadow_env_t(const shared_ptr<const lmb_t> &_shadow_val, const env_t &_orgi_env) :
        shadow_val(_shadow_val), orgi_env(_orgi_env) {}

    const shared_ptr<const lmb_t>& operator[](int idx) const {
        return idx == 0 ? shadow_val : orgi_env[idx-1];
    }
};

struct expr_t {
    virtual shared_ptr<const lmb_t> eval(const shadow_env_t &env) const = 0;
    virtual ~expr_t() {};
};

struct lmb_t {

    expr_t *body;
    const env_t env;

    lmb_t(const lmb_t &ref) :
        body(ref.body), env(ref.env) {}

    lmb_t(expr_t *_body, const env_t &_env) :
        body(_body), env(_env) {}

    shared_ptr<const lmb_t> exec(const shared_ptr<const lmb_t> &arg) const {
        return body->eval(shadow_env_t(arg, env));
    }
};

struct lmb_expr_t : public expr_t {

    expr_t *body;
    vector<int> arg_map;
    static map<tuple<expr_t*, env_t>, shared_ptr<const lmb_t>> cache;

    lmb_expr_t(expr_t *_body, const vector<int> &_arg_map) :
        body(_body), arg_map(_arg_map) {}

    virtual shared_ptr<const lmb_t> eval(const shadow_env_t &env) const {

        env_t nenv(arg_map.size());
        for (int i = 0; i < (int)arg_map.size(); i++)
            nenv[i] = env[arg_map[i]];

        auto key = make_tuple(body, nenv);
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
        else
            return cache[key] = make_shared<lmb_t>(body, nenv);
    }

    static void clear_cache() {
        cache.clear();
    }
};
map<tuple<expr_t*, env_t>, shared_ptr<const lmb_t>> lmb_expr_t::cache;

struct apply_expr_t : public expr_t {

    static bool pure;
    static map<pair<shared_ptr<const lmb_t>, shared_ptr<const lmb_t>>, shared_ptr<const lmb_t>> cache;

    expr_t* func;
    expr_t* arg;

    apply_expr_t(expr_t *_func, expr_t *_arg) :
        func(_func), arg(_arg) {}

    virtual shared_ptr<const lmb_t> eval(const shadow_env_t &env) const {

        auto lfunc = func->eval(env);
        auto larg = arg->eval(env);

        auto key = make_pair(lfunc, larg);
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;

        bool _pure = pure;
        pure = true;
        auto retv = lfunc->exec(larg);

        if (pure) {
            pure = _pure;
            return cache[key] = retv;
        } else {
            pure = false;
            return retv;
        }
    }

    static void clear_cache() {
        cache.clear();
    }
};
bool apply_expr_t::pure = true;
map<pair<shared_ptr<const lmb_t>, shared_ptr<const lmb_t>>, shared_ptr<const lmb_t>> apply_expr_t::cache;

struct ref_expr_t : public expr_t {

    int ref_idx;

    ref_expr_t(int _ref_idx) : ref_idx(_ref_idx) {}

    virtual shared_ptr<const lmb_t> eval(const shadow_env_t &env) const {
        return env[ref_idx];
    }
};

// parser

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

    map<tuple<int>, expr_t*> cache_ref;
    map<tuple<vector<int>, expr_t*>, expr_t*> cache_lmb;
    map<tuple<expr_t*, expr_t*>, expr_t*> cache_apply;

    parser_t() {}

    string indent(int depth) {
        return string(depth * 1, ' ');
    }

    expr_t *parse_single_expr(tokenizer_t &tok, map<string, int> &ref, int depth=0) {

        string token = tok.pop();
        //cerr << indent(depth) << "tok: " << token << endl;

        assert(token != "");

        if (token == "(") {
            //cerr << indent(depth) << "<<app" << endl;
            auto retv = parse_expr(tok, ref, depth+1);
            //cerr << indent(depth) << "app>>" << endl;
            tok.pop();
            return retv;
        }
        if (token != "\\") {
            //cerr << indent(depth) << "ref: " << token << endl;
            if (!ref.count(token))
                ref.insert(make_pair(token, ref.size()));
            auto key = make_tuple(ref[token]);
            auto it = cache_ref.find(key);
            if (it != cache_ref.end())
                return it->second;
            else
                return cache_ref[key] = new ref_expr_t(ref[token]);
        }

        // lambda

        map<string, int> nref;
        string arg = tok.pop();
        //cerr << indent(depth) << "def: " << arg << endl;
        //cerr << indent(depth) << "<<lmb" << endl;
        nref[arg] = 0;
        expr_t *body = parse_expr(tok, nref, depth+1);
        //cerr << indent(depth) << "lmb>>" << endl;

        nref.erase(arg);
        vector<int> arg_map(nref.size());
        for (auto pair : nref) {
            if (!ref.count(pair.first))
                ref.insert(make_pair(pair.first, ref.size()));
            //cerr << indent(depth) << "cap: " << pair.first << endl;
            arg_map[pair.second-1] = ref[pair.first];
        }

        auto key = make_tuple(arg_map, body);
        auto it = cache_lmb.find(key);
        if (it != cache_lmb.end())
            return it->second;
        else
            return cache_lmb[key] = new lmb_expr_t(body, arg_map);
    }

    expr_t *parse_expr(tokenizer_t &tok, map<string, int> &ref, int depth=0) {

        //cerr << indent(depth) << "<<func" << endl;
        expr_t *func = parse_single_expr(tok, ref, depth+1);
        //cerr << indent(depth) << "func>>" << endl;
        while (tok.peak() != ")") {
            assert(tok.peak() != "");
            //cerr << indent(depth) << "<<arg" << endl;
            auto arg = parse_single_expr(tok, ref, depth+1);
            auto key = make_tuple(func, arg);
            auto it = cache_apply.find(key);
            if (it != cache_apply.end()) {
                func = it->second;
            } else
                func = cache_apply[key] = new apply_expr_t(func, arg);
            //cerr << indent(depth) << "arg>>" << endl;
        }

        return func;
    }

    bool run_once(tokenizer_t &tok, map<string, shared_ptr<const lmb_t>> &env) {

        if (tok.peak() == "")
            return false;

        map<string, int> ref;
        ref[""] = 0; // nullptr arg
        expr_t *prog = parse_single_expr(tok, ref);
        ref.erase("");

        env_t nenv(ref.size());
        for (auto pair : ref) {
            //cerr << "gbl: " << pair.first << endl;
            assert(env.count(pair.first));
            nenv[pair.second-1] = env[pair.first];
        }

        prog->eval(shadow_env_t(nullptr, nenv));
        apply_expr_t::clear_cache();
        lmb_expr_t::clear_cache();
        apply_expr_t::pure = true;
        return true;
    }
};

// runtime

void output(int bit) {

    static int pos = 7;
    static int val = 0;

    val |= (bit << pos--);
    if (pos < 0) {
        cout << char(val);
        cout.flush();
        pos = 7, val = 0;
    }

    apply_expr_t::pure = false;
}

int input() {

    // FIXME: how about eof?

    static int pos = -1;
    static int val = 0;

    if (pos < 0) {
        pos = 7;
        val = cin.get();
        //cerr << "input: " << val << endl;
    }

    apply_expr_t::pure = false;
    return (val >> pos--) & 1;
}


struct builtin_p0_expr_t : public expr_t {
    virtual shared_ptr<const lmb_t> eval(const shadow_env_t &env) const {
        output(0);
        return env[0];
    }
};

struct builtin_p1_expr_t : public expr_t {
    virtual shared_ptr<const lmb_t> eval(const shadow_env_t &env) const {
        output(1);
        return env[0];
    }
};

struct builtin_g_expr_t : public expr_t {
    virtual shared_ptr<const lmb_t> eval(const shadow_env_t &env) const {
        return env[input()+1];
    }
};

// main

int main(int argc, char *args[]) {

    assert(argc > 1);
    fstream fin(args[1]);

    tokenizer_t toks(fin);
    parser_t parser;

    map<string, shared_ptr<const lmb_t>> env;
    env["__builtin_p0"] = make_shared<lmb_t>(new builtin_p0_expr_t(), env_t{});
    env["__builtin_p1"] = make_shared<lmb_t>(new builtin_p1_expr_t(), env_t{});
    env["__builtin_g"] = make_shared<lmb_t>(new lmb_expr_t(new lmb_expr_t(new builtin_g_expr_t(), {1, 0}), {0}), env_t{});

    while (parser.run_once(toks, env));
}
