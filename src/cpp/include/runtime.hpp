#ifndef __RUNTIME_H__
#define __RUNTIME_H__

#include <map>
#include <array>
#include <iostream>

struct lmb_t {
    lmb_t *(*exec)(lmb_t *env[], lmb_t *arg);
    unsigned long ref_cnt;
    lmb_t *envs[0];
};

template <int id, int n>
static lmb_t *create(std::array<long, n+3>&& ref) {

    std::map<std::array<long, n+3>, lmb_t*> cache;

    auto it = cache.find(ref);
    if (it != cache.end()) {
        ++it->second->ref_cnt;
        return it->second;
    } else {
        return cache[ref] = (lmb_t*)(new std::array<long, n+3>(ref));
    }
}

static bool __is_pure = true;

static inline void output(int bit) {

    static int pos = 7;
    static int val = 0;

    val |= (bit << pos--);
    if (pos < 0) {
        std::cout << char(val);
        std::cout.flush();
        pos = 7, val = 0;
    }

    __is_pure = false;
}

static inline int input() {

    // FIXME: how about eof?

    static int pos = EOF;
    static int val = 0;

    if (pos < 0) {
        val = std::cin.get();
        if (val == EOF)
            return 1;
        pos = 7;
    }

    __is_pure = false;
    return (val >> pos--) & 1;
}

static lmb_t *__do_builtin_p0(lmb_t **, lmb_t *w) {
    output(0);
    ++w->ref_cnt;
    return w;
}

static lmb_t *__do_builtin_p1(lmb_t **, lmb_t *w) {
    output(1);
    ++w->ref_cnt;
    return w;
}

static lmb_t *__do_builtin_g2(lmb_t **e, lmb_t *c) {
    int b = input();
    lmb_t *retv = (b == EOF) ? c : e[b];
    ++retv->ref_cnt;
    return retv;
}

static lmb_t *__do_builtin_g1(lmb_t **e, lmb_t *a1) {
    return create<-1, 2>({{(long)__do_builtin_g2, 1, (long)e[0], (long)a1, 0}});
}

static lmb_t *__do_builtin_g(lmb_t **, lmb_t *a0) {
    return create<-1, 1>({{(long)__do_builtin_g1, 1, (long)a0, 0}});
}

static inline void release(struct lmb_t *lmb) {
    if (!--lmb->ref_cnt)
        for (int i = 0; lmb->envs[i]; i++)
            release(lmb->envs[i]);
}

static inline lmb_t *apply(lmb_t *a, lmb_t *b) {

    static std::map<std::pair<lmb_t*, lmb_t*>, lmb_t*> caches;

    auto key = std::make_pair(a, b);
    auto it = caches.find(key);
    if (it != caches.end())
        return it->second;

    bool __ref_pure = __is_pure;
    __is_pure = true;

    struct lmb_t *retv = a->exec(a->envs, b);
    if (__is_pure) {
        caches[key] = retv;
        ++retv->ref_cnt;
        __is_pure = __ref_pure;
    } else {
        __is_pure = false;
    }
    return retv;
}

static lmb_t *__builtin_g(create<-1, 0>({{(long)__do_builtin_g, 1, 0}}));
static lmb_t *__builtin_p0(create<-1, 0>({{(long)__do_builtin_p0, 1, 0}}));
static lmb_t *__builtin_p1(create<-1, 0>({{(long)__do_builtin_p1, 1, 0}}));

#endif
