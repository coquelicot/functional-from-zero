#include "tokenizer.hpp"
#include <map>
#include <cctype>

tokenizer_t::tokenizer_t(std::istream &_stm) : stm(_stm) {}

token_t tokenizer_t::pop() {
    token_t retv = peak();
    if (!buf.empty())
        buf.pop();
    return retv;
}

token_t tokenizer_t::peak() {
    while (buf.empty())
        if (!__parse_more())
            return token_t{token_t::END};
    return buf.front();
}

bool tokenizer_t::__parse_more() {

    static std::map<char, token_t::type_t> spe_chars = {
        {'(', token_t::LEFT_BRACKET},
        {')', token_t::RIGHT_BRACKET},
        {'\\', token_t::BACK_SLASH},
    };

    std::string line;
    do {
        if (!std::getline(stm, line))
            return false;
    } while (__is_comment(line));

    for (size_t i = 0; i < line.length(); i++) {

        if (isspace(line[i]))
            continue;

        if (spe_chars.count(line[i])) {
            buf.push(token_t{spe_chars[line[i]], std::string(1, line[i])});
            continue;
        }

        size_t j = i + 1;
        while (j < line.length() && !isspace(line[j]) && !spe_chars.count(line[j]))
            j++;

        buf.push(token_t{token_t::IDENTIFIER, std::string(line.begin() + i, line.begin() + j)});
        i = j - 1;
    }

    return true;
}

bool tokenizer_t::__is_comment(std::string &line) {
    for (auto chr : line)
        if (!isspace(chr))
            return chr == '#';
    return false;
}
