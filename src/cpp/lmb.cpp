#include <istream>
#include <sstream>
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include <cctype>
#include <cassert>

using namespace std;

struct expr_t;
struct lmb_expr_t;
struct lmb_t {
    virtual shared_ptr<lmb_t> exec(shared_ptr<lmb_t> arg) = 0;
    virtual ~lmb_t() {}
};

struct val_lmb_t : public lmb_t {

    int arg_idx;
    shared_ptr<expr_t> body;
    vector<shared_ptr<lmb_t>> env;

    val_lmb_t(int _arg_idx, shared_ptr<expr_t> &_body, vector<shared_ptr<lmb_t>> &_env) :
        arg_idx(_arg_idx), body(_body), env(_env) {}

    virtual shared_ptr<lmb_t> exec(shared_ptr<lmb_t> arg);
};

struct expr_t {
    virtual shared_ptr<lmb_t> eval(const vector<shared_ptr<lmb_t>> &env) = 0;
    virtual ~expr_t() {};
};

struct lmb_expr_t : public expr_t {

    int arg_idx;
    vector<int> arg_map;
    shared_ptr<expr_t> body;

    lmb_expr_t(int _arg_idx, vector<int> &_arg_map, shared_ptr<expr_t> &_body) :
        arg_idx(_arg_idx), arg_map(_arg_map), body(_body) {}

    virtual shared_ptr<lmb_t> eval(const vector<shared_ptr<lmb_t>> &env) {
        vector<shared_ptr<lmb_t>> nenv(arg_map.size());
        for (int i = 0; i < (int)arg_map.size(); i++)
            if (arg_map[i] >= 0)
                nenv[i] = env[arg_map[i]];
        return make_shared<val_lmb_t>(arg_idx, body, nenv);
    }
};

struct apply_expr_t : public expr_t {

    shared_ptr<expr_t> func;
    vector<shared_ptr<expr_t>> args;

    apply_expr_t(shared_ptr<expr_t> &_func, vector<shared_ptr<expr_t>> &_args) :
        func(_func), args(_args) {}

    virtual shared_ptr<lmb_t> eval(const vector<shared_ptr<lmb_t>> &env) {
        shared_ptr<lmb_t> lmb = func->eval(env);
        for (auto arg : args) {
            assert(lmb.get());
            lmb = lmb->exec(arg->eval(env));
        }
        return lmb;
    }
};

struct ref_expr_t : public expr_t {

    int ref_idx;

    ref_expr_t(int _ref_idx) : ref_idx(_ref_idx) {}

    virtual shared_ptr<lmb_t> eval(const vector<shared_ptr<lmb_t>> &env) {
        return env[ref_idx];
    }
};

shared_ptr<lmb_t> val_lmb_t::exec(shared_ptr<lmb_t> arg) {
    vector<shared_ptr<lmb_t>> nenv = env;
    if (arg_idx >= 0)
        nenv[arg_idx] = arg;
    return body->eval(nenv);
}

struct tokenizer_t {

    string next;
    stringstream stm;

    tokenizer_t(stringstream &_stm) {
        stm << _stm.rdbuf();
    }

    string pop() {
        string retv = peak();
        next = extract_next();
        return retv;
    }

    string peak() {
        if (next == "")
            next = extract_next();
        return next;
    }

    string extract_next() {

        char c;
        do {
            if (!stm.get(c))
                return "";
        } while (isspace(c));

        if (string("()\\").find(c) != string::npos)
            return string(1, c);

        string ident;
        do {
            ident += c;
            if (!stm.get(c))
                return ident;
        } while (!isspace(c) && string("()\\").find(c) == string::npos);

        stm.unget();
        return ident;
    }

};

string indent(int depth) {
    return string(depth * 1, ' ');
}

shared_ptr<expr_t> compile(tokenizer_t *tok, map<string, int> &ref, int depth=0) {

    string token = tok->pop();
    cerr << indent(depth) << "tok: " << token << endl;

    if (token == "(") {

        cerr << indent(depth) << "<<func" << endl;
        shared_ptr<expr_t> func = compile(tok, ref, depth+1);
        cerr << indent(depth) << "func>>" << endl;
        vector<shared_ptr<expr_t>> args;
        while (tok->peak() != ")") {
            cerr << indent(depth) << "<<arg" << endl;
            args.push_back(compile(tok, ref, depth+1));
            cerr << indent(depth) << "arg>>" << endl;
        }
        tok->pop();

        return make_shared<apply_expr_t>(func, args);

    } else if (token == "\\") {

        map<string, int> nref;
        string arg = tok->pop();
        cerr << indent(depth) << "def: " << arg << endl;
        cerr << indent(depth) << "<<lmb" << endl;
        shared_ptr<expr_t> body = compile(tok, nref, depth+1);
        cerr << indent(depth) << "lmb>>" << endl;

        vector<int> arg_map(nref.size());
        for (auto pair : nref) {
            if (pair.first == arg) {
                arg_map[pair.second] = -1;
            } else {
                if (!ref.count(pair.first))
                    ref.insert(make_pair(pair.first, ref.size()));
                cerr << indent(depth) << "cap: " << pair.first << endl;
                arg_map[pair.second] = ref[pair.first];
            }
        }

        int idx = nref.count(arg) ? nref[arg] : -1;
        return make_shared<lmb_expr_t>(idx, arg_map, body);

    } else {
        cerr << indent(depth) << "ref: " << token << endl;
        if (!ref.count(token))
            ref.insert(make_pair(token, ref.size()));
        return make_shared<ref_expr_t>(ref[token]);
    }

}

void shift(int bit) {

    static int pos = 7;
    static int val = 0;

    val |= (bit << pos--);
    if (pos < 0) {
        cout << char(val);
        cout.flush();
        pos = 7, val = 0;
    }
}

struct put0_lmb_t : public lmb_t {
    virtual shared_ptr<lmb_t> exec(shared_ptr<lmb_t> arg) {
        shift(0);
        return nullptr;
    }
};

struct put1_lmb_t : public lmb_t {
    virtual shared_ptr<lmb_t> exec(shared_ptr<lmb_t> arg) {
        shift(1);
        return nullptr;
    }
};

int main() {

    string line;
    stringstream ss;
    while (getline(cin, line)) {
        if (line.length() && line[0] == '#')
            continue;
        ss << line;
    }

    map<string, int> ref;
    shared_ptr<expr_t> prog = compile(new tokenizer_t(ss), ref);

    for (auto p : ref)
        cerr << "gbl: " << p.first << endl;

    vector<shared_ptr<lmb_t>> env(ref.size());
    if (ref.count("p0")) {
        env[ref["p0"]] = make_shared<put0_lmb_t>();
        ref.erase("p0");
    }
    if (ref.count("p1")) {
        env[ref["p1"]] = make_shared<put1_lmb_t>();
        ref.erase("p1");
    }

    assert(ref.size() == 0);
    prog->eval(env);
}
