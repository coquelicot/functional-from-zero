#ifndef __AST_H__
#define __AST_H__

#include <string>
#include <vector>
#include <memory>

struct node_t {
    virtual ~node_t() {};
    virtual void print(int depth=0) = 0;
};

using node_hdr_t = std::shared_ptr<node_t>;

struct apply_node_t : public node_t {

    node_hdr_t nd_fun;
    node_hdr_t nd_arg;

    apply_node_t(node_hdr_t _nd_fun, node_hdr_t _nd_arg);
    virtual void print(int depth);
};

struct ref_node_t : public node_t {

    std::string ident;

    ref_node_t(std::string &_ident);
    virtual void print(int depth);
};

struct lmb_node_t : public node_t {

    std::string arg;
    node_hdr_t nd_body;

    lmb_node_t(std::string &_arg, node_hdr_t _nd_body);
    virtual void print(int depth);
};


#endif
