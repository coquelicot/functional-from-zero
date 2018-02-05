#ifndef __TRANSPILER_H__
#define __TRANSPILER_H__

#include "ast.hpp"
#include <string>
#include <sstream>
#include <ostream>
#include <set>
#include <vector>
#include <map>
#include <memory>

struct transpiler_t {

    std::set<std::string> builtins;

    transpiler_t(std::set<std::string> &_builtins);

    void transpile(node_hdr_t node, std::ostream &stm);

private:
    struct impl_t;
    std::shared_ptr<impl_t> impl;
};

#endif
