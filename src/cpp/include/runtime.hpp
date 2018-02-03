#ifndef __RUNTIME_H__
#define __RUNTIME_H__

#include <map>
#include <unordered_map>
#include <array>
#include <tuple>
#include <memory>
#include <iostream>

using namespace std;

struct lmb_t;
using lmb_hdr_t = shared_ptr<const lmb_t>;

template <int n>
using env_t = array<lmb_hdr_t, n>;

template <typename T, typename... Args>
inline shared_ptr<T> make_lmb(Args&&... args) {

    static map<tuple<Args...>, shared_ptr<T>> cache;

    auto key = make_tuple(args...);
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;
    else
        return cache[key] = make_shared<T>(std::forward<Args>(args)...);
}

struct lmb_t {

    static bool pure;

    mutable unordered_map<lmb_hdr_t, lmb_hdr_t> cache;

    lmb_hdr_t cached_exec(const lmb_hdr_t &arg) const {

        auto it = cache.find(arg);
        if (it != cache.end())
            return it->second;

        bool _pure = pure;
        pure = true;
        auto retv = exec(arg);

        if (pure) {
            pure = _pure;
            return cache[arg] = retv;
        } else {
            pure = false;
            return retv;
        }
    }

    virtual lmb_hdr_t exec(const lmb_hdr_t &arg) const = 0;
    virtual ~lmb_t() {}
};
bool lmb_t::pure = true;

static inline void output(int bit) {

    static int pos = 7;
    static int val = 0;

    val |= (bit << pos--);
    if (pos < 0) {
        cout << char(val);
        cout.flush();
        pos = 7, val = 0;
    }

    lmb_t::pure = false;
}

static inline int input() {

    // FIXME: how about eof?

    static int pos = EOF;
    static int val = 0;

    if (pos < 0) {
        val = cin.get();
        if (val == EOF)
            return 1;
        pos = 7;
    }

    lmb_t::pure = false;
    return (val >> pos--) & 1;
}

struct __builtin_p0_t : public lmb_t {
    virtual lmb_hdr_t exec(const lmb_hdr_t &arg) const {
        output(0);
        return arg;
    }
};

struct __builtin_p1_t : public lmb_t {
    virtual lmb_hdr_t exec(const lmb_hdr_t &arg) const {
        output(1);
        return arg;
    }
};

struct __builtin_g2_t : public lmb_t {
    env_t<2> env;
    __builtin_g2_t(const env_t<2> &_env) : env(_env) {}
    virtual lmb_hdr_t exec(const lmb_hdr_t &) const {
        return env[input()];
    }
};

struct __builtin_g1_t : public lmb_t {
    env_t<1> env;
    __builtin_g1_t(const env_t<1> &_env) : env(_env) {}
    virtual lmb_hdr_t exec(const lmb_hdr_t &arg) const {
        return make_lmb<__builtin_g2_t>(env_t<2>{{env[0], arg}});
    }
};

struct __builtin_g0_t : public lmb_t {
    virtual lmb_hdr_t exec(const lmb_hdr_t &arg) const {
        return make_lmb<__builtin_g1_t>(env_t<1>{{arg}});
    }
};

static lmb_hdr_t __builtin_g = make_lmb<__builtin_g0_t>();
static lmb_hdr_t __builtin_p0 = make_lmb<__builtin_p0_t>();
static lmb_hdr_t __builtin_p1 = make_lmb<__builtin_p1_t>();

#endif
