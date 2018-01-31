#include "runtime.hpp"
#include <iostream>

static int input() {

    // FIXME: how about eof?

    static int pos = -1;
    static int val = 0;

    if (pos < 0) {
        val = std::cin.get();
        if (val == -1)
            return -1;
        pos = 7;
    }

    return (val >> pos--) & 1;
}

static void output(int bit) {

    static int pos = 7;
    static int val = 0;

    val |= (bit << pos--);
    if (pos < 0) {
        std::cout << char(val);
        std::cout.flush();
        pos = 7, val = 0;
    }
}

std::shared_ptr<lmb_t> __builtin_p0_t::exec(std::shared_ptr<lmb_t> w) {
    output(0);
    return w;
}

std::shared_ptr<lmb_t> __builtin_p1_t::exec(std::shared_ptr<lmb_t> w) {
    output(1);
    return w;
}

__builtin_g1_t::__builtin_g1_t(std::shared_ptr<lmb_t> &_a0) : a0(_a0) {}
__builtin_g2_t::__builtin_g2_t(std::shared_ptr<lmb_t> &_a0, std::shared_ptr<lmb_t> &_a1) : a0(_a0), a1(_a1) {}

std::shared_ptr<lmb_t> __builtin_g2_t::exec(std::shared_ptr<lmb_t> c) {
    int b = input();
    return b == 0 ? a0 : b == 1 ? a1 : c;
}

std::shared_ptr<lmb_t> __builtin_g1_t::exec(std::shared_ptr<lmb_t> a1) {
    return std::make_shared<__builtin_g2_t>(a0, a1);
}

std::shared_ptr<lmb_t> __builtin_g_t::exec(std::shared_ptr<lmb_t> a0) {
    return std::make_shared<__builtin_g1_t>(a0);
}

std::shared_ptr<lmb_t> __builtin_g(new __builtin_g_t());
std::shared_ptr<lmb_t> __builtin_p0(new __builtin_p0_t());
std::shared_ptr<lmb_t> __builtin_p1(new __builtin_p1_t());
