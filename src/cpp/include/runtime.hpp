#ifndef __RUNTIME_H__
#define __RUNTIME_H__

#include <memory>

struct lmb_t {
    virtual std::shared_ptr<lmb_t> exec(std::shared_ptr<lmb_t>) = 0;
    virtual ~lmb_t() {};
};

struct __builtin_p0_t : public lmb_t {
    std::shared_ptr<lmb_t> exec(std::shared_ptr<lmb_t>);
};

struct __builtin_p1_t : public lmb_t {
    std::shared_ptr<lmb_t> exec(std::shared_ptr<lmb_t>);
};

struct __builtin_g_t : public lmb_t {
    std::shared_ptr<lmb_t> exec(std::shared_ptr<lmb_t>);
};

extern std::shared_ptr<lmb_t> __builtin_g;
extern std::shared_ptr<lmb_t> __builtin_p0;
extern std::shared_ptr<lmb_t> __builtin_p1;

std::shared_ptr<lmb_t> apply(std::shared_ptr<lmb_t> a, std::shared_ptr<lmb_t> b);

#endif
