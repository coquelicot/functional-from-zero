#include <fstream>
#include <sstream>
#include <iostream>
#include <deque>
#include <vector>
#include <map>
#include <memory>
#include <cctype>
#include <cassert>
#include <functional>

using namespace std;

struct expr_t;
struct lmb_t {
    virtual shared_ptr<lmb_t> exec(shared_ptr<lmb_t> arg) = 0;
    virtual ~lmb_t() {}
};

using env_t = vector<shared_ptr<lmb_t>>;
struct shadow_env_t {

    int shadow_idx;
    shared_ptr<lmb_t> shadow_val;
    env_t &orgi_env;

    shadow_env_t(env_t &_orgi_env, int _shadow_idx=-1, shared_ptr<lmb_t> _shadow_val=nullptr)
        : shadow_idx(_shadow_idx), shadow_val(_shadow_val), orgi_env(_orgi_env) {}

    const shared_ptr<lmb_t>& operator[](int idx) const {
        return idx == shadow_idx ? shadow_val : orgi_env[idx];
    }
};

struct val_lmb_t : public lmb_t {

    int arg_idx;
    shared_ptr<expr_t> body;
    env_t env;

    val_lmb_t(int _arg_idx, shared_ptr<expr_t> &_body, env_t &_env) :
        arg_idx(_arg_idx), body(_body), env(_env) {}

    virtual shared_ptr<lmb_t> exec(shared_ptr<lmb_t> arg);
};

struct expr_t {
    virtual shared_ptr<lmb_t> eval(const shadow_env_t &env) = 0;
    virtual ~expr_t() {};
};

struct lmb_expr_t : public expr_t {

    int arg_idx;
    vector<int> arg_map;
    shared_ptr<expr_t> body;

    lmb_expr_t(int _arg_idx, vector<int> &_arg_map, shared_ptr<expr_t> &_body) :
        arg_idx(_arg_idx), arg_map(_arg_map), body(_body) {}

    virtual shared_ptr<lmb_t> eval(const shadow_env_t &env) {
        env_t nenv(arg_map.size());
        for (int i = 0; i < (int)arg_map.size(); i++)
            if (arg_map[i] >= 0)
                nenv[i] = env[arg_map[i]];
        return make_shared<val_lmb_t>(arg_idx, body, nenv);
    }
};

struct apply_expr_t : public expr_t {

    shared_ptr<expr_t> func;
    vector<shared_ptr<expr_t>> args;
    static map<pair<shared_ptr<lmb_t>, shared_ptr<lmb_t>>, shared_ptr<lmb_t>> rec_map;

    apply_expr_t(shared_ptr<expr_t> &_func, vector<shared_ptr<expr_t>> &_args) :
        func(_func), args(_args) {}

    virtual shared_ptr<lmb_t> eval(const shadow_env_t &env) {
        shared_ptr<lmb_t> lmb = func->eval(env);
        for (auto arg : args) {
            assert(lmb.get());
            lmb = apply(lmb, arg->eval(env));
        }
        return lmb;
    }

    shared_ptr<lmb_t> apply(shared_ptr<lmb_t> func, shared_ptr<lmb_t> arg) {

        auto key = make_pair(func, arg);
        auto it = rec_map.find(key);

        if (it != rec_map.end())
            return it->second;
        else
            return rec_map[key] = func->exec(arg);
    }

    static void clear_cache() {
        rec_map.clear();
    }
};
map<pair<shared_ptr<lmb_t>, shared_ptr<lmb_t>>, shared_ptr<lmb_t>> apply_expr_t::rec_map;

struct ref_expr_t : public expr_t {

    int ref_idx;

    ref_expr_t(int _ref_idx) : ref_idx(_ref_idx) {}

    virtual shared_ptr<lmb_t> eval(const shadow_env_t &env) {
        return env[ref_idx];
    }
};

shared_ptr<lmb_t> val_lmb_t::exec(shared_ptr<lmb_t> arg) {
    return body->eval(shadow_env_t(env, arg_idx, arg));
}

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
        } while (line.length() == 0 || line[0] == '#');

        _parse(line);
        return true;
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

    parser_t() {}

    string indent(int depth) {
        return string(depth * 1, ' ');
    }

    shared_ptr<expr_t> parse_single_expr(tokenizer_t &tok, map<string, int> &ref, int depth=0) {

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
            return make_shared<ref_expr_t>(ref[token]);
        }

        // lambda

        map<string, int> nref;
        string arg = tok.pop();
        //cerr << indent(depth) << "def: " << arg << endl;
        //cerr << indent(depth) << "<<lmb" << endl;
        shared_ptr<expr_t> body = parse_expr(tok, nref, depth+1);
        //cerr << indent(depth) << "lmb>>" << endl;

        vector<int> arg_map(nref.size());
        for (auto pair : nref) {
            if (pair.first == arg) {
                arg_map[pair.second] = -1;
            } else {
                if (!ref.count(pair.first))
                    ref.insert(make_pair(pair.first, ref.size()));
                //cerr << indent(depth) << "cap: " << pair.first << endl;
                arg_map[pair.second] = ref[pair.first];
            }
        }

        int idx = nref.count(arg) ? nref[arg] : -1;
        return make_shared<lmb_expr_t>(idx, arg_map, body);
    }

    shared_ptr<expr_t> parse_expr(tokenizer_t &tok, map<string, int> &ref, int depth=0) {

        //cerr << indent(depth) << "<<func" << endl;
        shared_ptr<expr_t> func = parse_single_expr(tok, ref, depth+1);
        //cerr << indent(depth) << "func>>" << endl;
        vector<shared_ptr<expr_t>> args;
        while (tok.peak() != ")" && tok.peak() != "") {
            //cerr << indent(depth) << "<<arg" << endl;
            args.push_back(parse_single_expr(tok, ref, depth+1));
            //cerr << indent(depth) << "arg>>" << endl;
        }

        return make_shared<apply_expr_t>(func, args);
    }

    bool run_once(tokenizer_t &tok, map<string, shared_ptr<lmb_t>> &env) {

        if (tok.peak() == "")
            return false;

        map<string, int> ref;
        shared_ptr<expr_t> prog = parse_single_expr(tok, ref);

        env_t nenv(ref.size());
        for (auto pair : ref) {
            //cerr << "gbl: " << pair.first << endl;
            assert(env.count(pair.first));
            nenv[pair.second] = env[pair.first];
        }

        prog->eval(shadow_env_t(nenv));
        apply_expr_t::clear_cache();
        return true;
    }
};

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
        pos = 7;
        val = cin.get();
        //cerr << "input: " << val << endl;
    }

    return (val >> pos--) & 1;
}

struct native_lmb_t : public lmb_t {

    using argv_t = env_t;
    using func_t = function<shared_ptr<lmb_t>(argv_t&)>;

    size_t args;
    argv_t argv;
    func_t func;

    native_lmb_t(size_t _args, func_t _func, argv_t _argv=argv_t()) :
        args(_args), argv(_argv), func(_func) {}

    virtual shared_ptr<lmb_t> exec(shared_ptr<lmb_t> arg) {
        if (argv.size() == args) {
            // arg is the "world" arg
            return func(argv);
        } else {
            argv_t nargv = argv;
            nargv.push_back(arg);
            return make_shared<native_lmb_t>(args, func, nargv);
        }
    }
};

int main(int argc, char *args[]) {

    assert(argc > 1);
    fstream fin(args[1]);

    tokenizer_t toks(fin);
    parser_t parser;

    map<string, shared_ptr<lmb_t>> env;
    env["__builtin_p0"] = make_shared<native_lmb_t>(0, [] (native_lmb_t::argv_t &argv) {
        output(0);
        return nullptr;
    });
    env["__builtin_p1"] = make_shared<native_lmb_t>(0, [] (native_lmb_t::argv_t &argv) {
        output(1);
        return nullptr;
    });
    env["__builtin_g"] = make_shared<native_lmb_t>(2, [] (native_lmb_t::argv_t &argv) {
        return argv[input()];
    });

    while (parser.run_once(toks, env));
}
