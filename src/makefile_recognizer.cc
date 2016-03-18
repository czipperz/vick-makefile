#include "../../../src/contents.hh"
#include "../../../src/open_file.hh"
#include <algorithm>
#include <ostream>
#include <iostream>

namespace {

struct token {
    enum {
        ASSIGNMENT,
        BACKTICK_END,
        BACKTICK_START,
        COMMENT,
        EQUALS_SIGN,
        ERROR,
        NO_PRINT_CMD,
        SUBSHELL_END,
        SUBSHELL_START,
        VARNAME,
    } type;
    std::string value;
    vick::move_t y, x;

    friend std::ostream&
    operator<<(std::ostream& os, const token& tok)
    {
        os << "(" << tok.y << ", " << tok.x << ") ";
        switch (tok.type) {
#define c(x) case x: os << #x; break;
            c(ASSIGNMENT)
            c(BACKTICK_END)
            c(BACKTICK_START)
            c(COMMENT)
            c(EQUALS_SIGN)
            c(ERROR)
            c(NO_PRINT_CMD)
            c(SUBSHELL_END)
            c(SUBSHELL_START)
            c(VARNAME)
#undef c
        }
        return os << ": " << tok.value << std::endl;
    }

    bool operator<(const token& other) {
        if (y < other.y) return true;
        if (y > other.y) return false;
        if (/* y == other.y */ x < other.x) return true;
        return false;
    }

    bool operator>(const token& other) {
        if (y > other.y) return true;
        if (y < other.y) return false;
        if (/* y == other.y */ x > other.x) return true;
        return false;
    }

    bool operator<=(const token& other) {
        return not (*this > other);
    }

    bool operator>=(const token& other) {
        return not (*this < other);
    }
};

}

namespace vick {
namespace makefile {

/*
 * TODO:
 * add backtick and $$() handler.
 */
static void
recognize(std::string::const_iterator strbegin,
          std::string::const_iterator strloc,
          std::string::const_iterator strend,
          move_t y,
          std::vector<token>& vec)
{
top_of_method:
    if (strloc >= strend) return;
    auto size = std::distance(strbegin, strend);
    std::string::const_iterator end;

#define test(num, ch)                                          \
    /* test if actually has ch */                              \
    if ((end = std::find(strloc + num, strend, ch)) == strend) { \
        strloc += num;                                           \
        goto top_of_method;                                    \
    }                                                          \
    /* test if ch is escaped */                                \
    while (end[-1] == '\\') {                                  \
        if ((end = std::find(end + 1, strend, ch)) == strend) {\
            strloc += num;                                     \
            goto top_of_method;                                \
        }                                                      \
    }

    if (size >= 2 and strloc[0] == '$') {
        if (size >= 3 and strloc[1] == '{') {
            // ${}
            test(2, '}');
            // body
            vec.push_back({ token::VARNAME, std::string(strloc + 2, end),
                            y,
                            static_cast<move_t>(std::distance(strbegin, strloc)) });
            strloc = end + 1;
            goto top_of_method;
        } else if (size >= 3 and strloc[1] == '(' and
                   (end = std::find(strloc + 2, strend, ')')) != strend) {
            // $()
            test(2, ')');
            // body
            vec.push_back({ token::VARNAME, std::string(strloc + 2, end),
                            y,
                            static_cast<move_t>(std::distance(strbegin, strloc)) });
            strloc = end + 1;
            goto top_of_method;
        } else if (size >= 2 and strloc[1] != '{' and strloc[1] != '(' and strloc[1] != '$') {
            // $h
            vec.push_back({ token::VARNAME, std::string(1, strloc[1]),
                            y,
                            static_cast<move_t>(std::distance(strbegin, strloc)) });
            strloc += 2;
            goto top_of_method;
        } else if (size >= 4 and strloc[1] == '$' and strloc[2] == '(' and
                   (strloc == strbegin or strloc[-1] != '\\')) {
            // $$() shell command
            test(2, ')');
            // body
            vec.push_back({ token::SUBSHELL_START, "$$(",
                        y,
                        static_cast<move_t>(std::distance(strbegin, strloc)) });
            vec.push_back({ token::SUBSHELL_END, ")",
                        y,
                        static_cast<move_t>(std::distance(strbegin, end)) });
            strloc += 3;
            goto top_of_method;
        } else {
            // $$...
            goto find_some_more;
        }
    } else if ((strbegin == strloc or std::isspace(strloc[-1]))
               and size >= 1 and strloc[0] == '#') {
        // comment
        vec.push_back({ token::COMMENT, std::string(strloc, strend),
                        y,
                        static_cast<move_t>(std::distance(strbegin, strloc)) });
        return;
    } else if (std::isspace(*strloc)) {
        goto find_some_more_space;
    } else if (*strloc == '`' and
               (strloc == strbegin or strloc[-1] != '\\')) {
        // backtick shell command
        // test if actually has ` (end)
        if ((end = std::find(strloc + 1, strend, '`')) == strend or
            std::find_if(vec.begin(), vec.end(),
                         [&](token& e) {
                             return e.type == token::BACKTICK_END and
                                    e.y == y and
                                    strbegin + e.x == strloc;
                         }) != vec.end() /* true if already parsed current backticks */) {
            ++strloc;
            goto top_of_method;
        }
        // test if ` (end) is escaped
        while (end[-1] == '\\') {
            end = std::find(end + 1, strend, '`');
            if (end == strend) {
                ++strloc;
                goto top_of_method;
            }
        }
        // body
        vec.push_back({ token::BACKTICK_START, "`",
                    y,
                    static_cast<move_t>(std::distance(strbegin, strloc)) });
        vec.push_back({ token::BACKTICK_END, "`",
                    y,
                    static_cast<move_t>(std::distance(strbegin, end)) });
        strloc++;
        goto top_of_method;
    } else if (*strbegin == '\t') {
        // shell command for routine that is not a comment
        while (++strloc != strend and std::isspace(*strloc)) {}
        if (strloc == strend) return;
        if (*strloc == '@') {
            vec.push_back({ token::NO_PRINT_CMD, std::string(1, *strloc),
                            y,
                            static_cast<move_t>(std::distance(strbegin, strloc)) });
            strloc++;
        }
        goto find_some_more;
    } else if ((end = std::find(strloc, strend, '=')) != strend) {
        // checks if actually assignment and then will add
        // valid: "hi = bye", " hi:=bye", "  hi ::= bye", "hi="
        // invalid: "\thi = bye", "${hi} ::= bye"
        bool at_beginning = true, at_ending = false;
        std::string::const_iterator begin = strend, end;
        for (auto i = strloc; i != strend; ++i) {
            if (std::isspace(*i)) {
                if (not at_beginning)
                    at_ending = true;
                continue;
            }
            if (not at_beginning and
                ((*i == '=') or
                 (i + 1 != strend and i[0] == ':' and i[1] == '=') or
                 (i + 2 != strend and i[0] == ':' and i[1] == ':' and
                  i[2] == '='))) {
                vec.push_back({ token::ASSIGNMENT, std::string(begin, end + 1),
                                y,
                                static_cast<move_t>(std::distance(strbegin, begin)) });
                if (i + 2 != strend and i[0] == ':' and i[1] == ':' and i[2] == '=') {
                    strloc = end + 4;
                } else if (i + 1 != strend and i[0] == ':' and i[1] == '=') {
                    strloc = end + 3;
                } else {
                    strloc = end + 2;
                }
                goto top_of_method;
            }
            if (not at_ending and std::isalnum(*i)) {
                at_beginning = false;
                if (i < begin) begin = i;
                end = i;
                continue;
            }
            break;
        }
    } else {
        // rules: "all: ${hi}" -> "all", "  .PHONY: x" -> ".PHONY"
        bool at_beginning = true;
        for (auto i = strbegin; i != strend; ++i) {
            if (at_beginning) {
                if (*i == '\t') {
                    ++strloc;
                    goto find_some_more;
                }
                /* else */if (std::isspace(*i)) continue;
                /* else */at_beginning = false;
            } else {
                // not tab indented and at one past first nonspace char
                break;
            }
        }
        end = std::find(strloc, strend, ':');
        if (end == strend) {
            ++strloc;
            goto find_some_more;
        }
        auto i = strloc;
    }

find_some_more:
    if (std::isspace(*strloc)) {
    find_some_more_space:
        while (++strloc != strend and std::isspace(*strloc)) {}
        goto top_of_method; //top checks strloc and strend to be equal
    }
    for (auto i = strloc; i != strend; ++i) {
        if (*i == '$' or *i == '`' or *i == '#' or *i == ':') {
            strloc = i;
            goto top_of_method;
        }
    }

    return;
}

std::vector<token> recognize(const contents& contents) {
    std::vector<token> tokens;
    tokens.reserve(contents.cont.size());
    for (auto itr = contents.cont.begin(); itr != contents.cont.end(); ++itr) {
        if (itr->empty()) continue;
#define rec(i, dist) recognize((i).begin(), (i).begin(), (i).end(),          \
                               dist, tokens)
        if (itr->end()[-1] == '\\') {
            std::string str (itr->begin(), itr->end() - 1);
            move_t dist = std::distance(contents.cont.begin(), itr);
            while (++itr != contents.cont.end()) {
                if (itr->empty()) break;
                if (itr->end()[-1] == '\\') {
                    str.insert(str.end(), itr->begin(), itr->end() - 1);
                } else {
                    str += *itr;
                    break;
                }
            }
            rec(str, dist);
        } else {
            rec(*itr, std::distance(contents.cont.begin(), itr));
        }
    }
    return tokens;
}

}
}

// int main() {
//     using namespace vick;
//     contents cont = open_file("Makefile");
//     auto tokens = makefile::recognize(cont);
//     std::sort(tokens.begin(), tokens.end());
//     for(auto& tok : tokens) { std::cout << tok; }
// }
