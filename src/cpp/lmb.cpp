#include <fstream>
#include <sstream>
#include <iostream>
#include <deque>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <utility>
#include <tuple>
#include <functional>
#include <atomic>
#include <random>
#include <thread>
#include <mutex>
#include <cctype>
#include <cstring>
#include <cassert>

using namespace std;

struct lmb_t;
using lmb_hdr_t = shared_ptr<lmb_t>;
using cache_t = pair<shared_ptr<atomic<bool>>, lmb_hdr_t>;
using cache_hdr_t = shared_ptr<cache_t>;

template <typename... Args>
lmb_hdr_t make_lmb(Args&&... args) {
    return make_shared<lmb_t>(std::forward<Args>(args)...);
}

using env_t = vector<lmb_hdr_t>;
struct shadow_env_t {

    const lmb_hdr_t &shadow_val;
    const env_t &orgi_env;

    shadow_env_t(const lmb_hdr_t &_shadow_val, const env_t &_orgi_env) :
        shadow_val(_shadow_val), orgi_env(_orgi_env) {}

    const lmb_hdr_t& operator[](int idx) const {
        return idx == 0 ? shadow_val : orgi_env[idx-1];
    }
};

struct env_hash_t {
    size_t operator()(const env_t &env) const {
        auto hasher = hash<lmb_hdr_t>();
        size_t retv = 0;
        for (auto &v : env)
            retv = retv * 17 + hasher(v);
        return retv;
    }
};

struct task_t;
struct worker_t;
struct expr_t {
    mutex lock;
    unordered_map<env_t, lmb_hdr_t, env_hash_t> cache;
    virtual void eval(task_t *task, worker_t *worker) const = 0;
    virtual ~expr_t() {};
};

// worker

struct task_t {
    atomic<int> *state;
    lmb_hdr_t *val;
    const shadow_env_t *env;
    const expr_t *expr;
};

struct task_que_t {

    struct buf_t {

        size_t const cap;
        atomic<task_t*> * const buf;

        buf_t(size_t _cap=256) : cap(_cap), buf(new atomic<task_t*>[cap]) {}

        static buf_t *extended(const buf_t *ref) {
            buf_t *retv = new buf_t(ref->cap * 2);
            for (size_t i = 0; i < ref->cap; i++) {
                task_t *val = ref->buf[i].load(memory_order_relaxed);
                retv->buf[i].store(val, memory_order_relaxed);
                retv->buf[i + ref->cap].store(val, memory_order_relaxed);
            }
            return retv;
        }
    };

    atomic<size_t> sid;
    atomic<size_t> eid;
    atomic<buf_t*> tasks;

    task_que_t() :
        sid(1), eid(1), tasks(new buf_t()) {}

    bool try_steal(task_t **retv) {
        size_t _sid = sid.load(memory_order_acquire);
        atomic_thread_fence(memory_order_seq_cst);
        size_t _eid = eid.load(memory_order_acquire);
        if (_sid < _eid) {
            buf_t *_tasks = tasks.load(memory_order_consume);
            *retv = _tasks->buf[_sid % _tasks->cap];
            return sid.compare_exchange_strong(_sid, _sid+1, memory_order_seq_cst, memory_order_relaxed);
        }
        return false;
    }

    bool try_pop(task_t **retv) {
        size_t _eid = eid.load(memory_order_relaxed) - 1;
        buf_t *_tasks = tasks.load(memory_order_relaxed);
        eid.store(_eid, memory_order_relaxed);
        atomic_thread_fence(memory_order_seq_cst);
        size_t _sid = sid.load(memory_order_relaxed);
        if (_sid <= _eid) {
            *retv = _tasks->buf[_eid % _tasks->cap].load(memory_order_relaxed);
            if (_sid == _eid) {
                bool succ = sid.compare_exchange_strong(_sid, _sid + 1, memory_order_seq_cst, memory_order_relaxed);
                eid.store(_eid + 1, memory_order_relaxed);
                return succ;
            } else {
                return true;
            }
        } else {
            eid.store(_eid + 1, memory_order_relaxed);
            return false;
        }
    }

    void push(task_t *ntask) {
        size_t _eid = eid.load(memory_order_relaxed);
        size_t _sid = sid.load(memory_order_relaxed);
        buf_t *_tasks = tasks.load(memory_order_relaxed);
        if (_eid - _sid >= _tasks->cap) {
            // XXX: never try to recycle memory
            _tasks = buf_t::extended(_tasks);
            tasks.store(_tasks, memory_order_seq_cst); // Is this correct?
        }
        _tasks->buf[_eid % _tasks->cap].store(ntask, memory_order_relaxed);
        atomic_thread_fence(memory_order_release);
        eid.store(_eid + 1, memory_order_relaxed);
    }
};

struct worker_t {

    task_que_t que;
    std::mt19937 prng;
    const vector<worker_t*> *workers;
    atomic<bool> *running;

    worker_t(atomic<bool> *_running) : running(_running) {}

    void tackle(task_t *task) {
        task->expr->eval(task, this);
    }

    task_t *get_next() {
        task_t *next;
        if (!que.try_pop(&next))
            if (workers->empty() || !(*workers)[uniform_int_distribution<size_t>(0, workers->size()-1)(prng)]->que.try_steal(&next))
                return NULL;
        return next;
    }

    void loop(const vector<worker_t*> &_workers, bool root) {
        workers = &_workers;
        for (task_t *next; running->load(); ) {
            if ((next = get_next()))
                tackle(next);
            else if (root)
                running->store(false);
        }
    }

    static void start(const vector<worker_t*> &workers, size_t idx, bool root) {

        vector<worker_t*> nworkers;
        for (size_t i = 0; i < workers.size(); i++)
            if (i != idx)
                nworkers.push_back(workers[i]);

        workers[idx]->loop(nworkers, root);
    }
};


struct lmb_t {
    const expr_t *body;
    const env_t env;
    mutex lock;
    unordered_map<lmb_hdr_t, cache_hdr_t> cache;
    lmb_t(expr_t *_body, const env_t &_env) :
        body(_body), env(_env) {}
};

struct lmb_expr_t : public expr_t {

    expr_t *body;
    vector<int> arg_map;

    lmb_expr_t(expr_t *_body, const vector<int> &_arg_map) :
        body(_body), arg_map(_arg_map) {}

    virtual void eval(task_t *task, worker_t *worker) const {

        env_t nenv(arg_map.size());
        for (int i = 0; i < (int)arg_map.size(); i++)
            nenv[i] = (*task->env)[arg_map[i]];

        {
            lock_guard<mutex> guard(body->lock);
            auto it = body->cache.find(nenv);
            if (it != body->cache.end())
                *task->val = it->second;
            else
                *task->val = body->cache[nenv] = make_shared<lmb_t>(body, nenv);
        }
        task->state->fetch_add(1);
    }
};

struct apply_expr_t : public expr_t {

    expr_t* func;
    expr_t* arg;

    apply_expr_t(expr_t *_func, expr_t *_arg) :
        func(_func), arg(_arg) {}

    virtual void eval(task_t *task, worker_t *worker) const {

        lmb_hdr_t lfunc;
        lmb_hdr_t larg;
        atomic<int> state(0);

        task_t tfunc{&state, &lfunc, task->env, func};
        task_t targ{&state, &larg, task->env, arg};
        worker->que.push(&targ);
        worker->tackle(&tfunc);

        for (task_t *next; state.load() != 2; )
            if ((next = worker->get_next()))
                worker->tackle(next);

        cache_hdr_t cache;
        bool do_calc = true;
        {
            lock_guard<mutex> guard(lfunc->lock);
            auto it = lfunc->cache.find(larg);
            if (it != lfunc->cache.end()) {
                cache = it->second;
                do_calc = false;
            } else {
                cache = lfunc->cache[larg] = make_shared<cache_t>(make_shared<atomic<bool>>(false), nullptr);
            }
        }

        if (!do_calc) {
            for (task_t *next; !cache->first->load(); )
                if ((next = worker->get_next()))
                    worker->tackle(next);
        } else {
            shadow_env_t env(larg, lfunc->env);
            task_t tapply{&state, &cache->second, &env, lfunc->body};
            worker->tackle(&tapply);
            cache->first->store(true);
        }

        *task->val = cache->second;
        task->state->fetch_add(1);
    }
};

struct ref_expr_t : public expr_t {

    int ref_idx;

    ref_expr_t(int _ref_idx) : ref_idx(_ref_idx) {}

    virtual void eval(task_t *task, worker_t *worker) const {
        *task->val = (*task->env)[ref_idx];
        task->state->fetch_add(1);
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

    expr_t *parse_single_expr(tokenizer_t &tok, map<string, int> &ref, int depth=0) {

        string token = tok.pop();
        assert(token != "");

        if (token == "(") {
            auto retv = parse_expr(tok, ref, depth+1);
            assert(tok.peak() == ")");
            tok.pop();
            return retv;
        }
        if (token != "\\") {
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
        assert(arg != "(" && arg != ")" && arg != "\\");
        nref[arg] = 0;
        expr_t *body = parse_expr(tok, nref, depth+1);

        nref.erase(arg);
        vector<int> arg_map(nref.size());
        for (auto pair : nref) {
            if (!ref.count(pair.first))
                ref.insert(make_pair(pair.first, ref.size()));
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

        expr_t *func = parse_single_expr(tok, ref, depth+1);
        while (tok.peak() != ")") {
            assert(tok.peak() != "");
            auto arg = parse_single_expr(tok, ref, depth+1);
            auto key = make_tuple(func, arg);
            auto it = cache_apply.find(key);
            if (it != cache_apply.end()) {
                func = it->second;
            } else
                func = cache_apply[key] = new apply_expr_t(func, arg);
        }

        return func;
    }

    bool run_once(tokenizer_t &tok, map<string, lmb_hdr_t> &env) {

        if (tok.peak() == "")
            return false;

        map<string, int> ref;
        ref[""] = 0; // nullptr arg
        expr_t *prog = parse_single_expr(tok, ref);
        ref.erase("");

        env_t nenv(ref.size());
        for (auto pair : ref) {
            if (!env.count(pair.first))
                std::cerr << pair.first << std::endl;
            assert(env.count(pair.first));
            nenv[pair.second-1] = env[pair.first];
        }

        lmb_hdr_t retv;
        atomic<int> state;
        shadow_env_t senv(nullptr, nenv);
        task_t task{&state, &retv, &senv, prog};

        // workers

        atomic<bool> running(true);
        vector<worker_t*> workers;
        for (int i = 0; i < 2; i++)
            workers.push_back(new worker_t(&running));
        workers[0]->que.push(&task);

        vector<thread*> threads;
        for (size_t i = 1; i < workers.size(); i++)
            threads.push_back(new thread(worker_t::start, std::ref(workers), i, false));
        worker_t::start(workers, 0, true);
        for (auto t : threads)
            t->join();

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
    virtual void eval(task_t *task, worker_t *worker) const {
        output(0);
        *task->val = (*task->env)[0];
        task->state->fetch_add(1);
    }
};

struct builtin_p1_expr_t : public expr_t {
    virtual void eval(task_t *task, worker_t *worker) const {
        output(1);
        *task->val = (*task->env)[0];
        task->state->fetch_add(1);
    }
};

struct builtin_g_expr_t : public expr_t {
    virtual void eval(task_t *task, worker_t *worker) const {
        int bit = input();
        *task->val = bit == EOF ? (*task->env)[3] : (*task->env)[bit+1];
        task->state->fetch_add(1);
    }
};

// main

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
