#include "parser.hpp"

static void merge_node(node_hdr_t &nd1, node_hdr_t nd2) {
    if (nd1 == nullptr)
        nd1 = nd2;
    else
        nd1 = node_hdr_t(new apply_node_t(nd1, nd2));
}

parser_t::parser_t() {
    states.push(state_t{state_t::ROOT});
}

parser_t::result_t parser_t::shift(token_t token) {

    if (token.type == token_t::LEFT_BRACKET) {
        if (states.top().type == state_t::LMB_IDENT) {
            return result_t{result_t::ERROR};
        } else {
            states.push(state_t{state_t::EXPR});
            return result_t{result_t::CONTINUE};
        }
    } else if (token.type == token_t::BACK_SLASH) {
        if (states.top().type == state_t::LMB_IDENT) {
            return result_t{result_t::ERROR};
        } else {
            states.push(state_t{state_t::LMB_IDENT});
            return result_t{result_t::CONTINUE};
        }
    } else if (token.type == token_t::IDENTIFIER) {
        if (states.top().type == state_t::LMB_IDENT) {
            states.top().ident = token.ident;
            states.top().type = state_t::LMB_EXPR;
            return result_t{result_t::CONTINUE};
        } else {
            merge_node(states.top().node, node_hdr_t(new ref_node_t(token.ident)));
        }
    } else if (token.type == token_t::RIGHT_BRACKET) {
        while (true) {

            if (states.top().type != state_t::EXPR && states.top().type != state_t::LMB_EXPR)
                return result_t{result_t::ERROR};

            state_t ref = states.top();
            states.pop();
            if (ref.node == nullptr)
                return result_t{result_t::ERROR};

            node_hdr_t node = ref.type == state_t::EXPR
                ? ref.node
                : node_hdr_t(new lmb_node_t(ref.ident, ref.node));
            merge_node(states.top().node, node);

            if (ref.type == state_t::EXPR)
                break;
        }
    } else { // END
        while (states.top().type != state_t::ROOT) {
            state_t ref = states.top();
            if (ref.type != state_t::LMB_EXPR || ref.node == nullptr)
                return result_t{result_t::ERROR};
            states.pop();
            merge_node(states.top().node, node_hdr_t(new lmb_node_t(ref.ident, ref.node)));
            states.top().node = node_hdr_t(new lmb_node_t(states.top().ident, states.top().node));
        }
    }

    if (states.top().type == state_t::ROOT) {
        result_t retv{result_t::DONE, states.top().node};
        states.top().node = nullptr;
        return retv;
    } else {
        return result_t{result_t::CONTINUE};
    }
}
