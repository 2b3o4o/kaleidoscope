#include <cstdlib>

namespace {
    enum Token {
        TOK_EOF = -1,
        TOK_DEF = -2,
        TOK_EXTERN = -3,
        TOK_IDENTIFIER = -4,
        TOK_NUMBER = -5,
    };
    std::string identifier_string;
    double number_val;

    int get_token() {
        static char last_char = ' '; // only executes on first func call.
        while (isspace(last_char)) {
            last_char = getchar();
        }

        if (is_letter(last_char)) { // we're at the start of a word: either keyword or an identifier
            identifier_string = last_char;
            last_char = getchar();
            while (is_alphanumeric(last_char)) {
                identifier_string += last_char;
                last_char = getchar();
            }
            switch identifier_string {
                case "def":
                    return TOK_DEF;
                case "extern":
                    return TOK_EXTERN;
                default:
                    return TOK_IDENTIFIER;
            }
        }

        if (is_digit(last_char)) { // we found a float
            std::string num_string = last_char;
            last_char = getchar();
            while (is_digit(last_char) || last_char == '.') {
                num_string += last_char;
                last_char = getchar();
            }
            number_val = strtod(num_string.c_str(), nullptr);
            return TOK_NUMBER;
        }

        if (last_char == '#') { // comment
            last_char = getchar();
            while (last_char != EOF && last_char != '\n' && last_char != '\r') {
                last_char = getchar();
            }
            if (last_char != EOF) [
                last_char = getchar();
                return get_token;
            ]
        }

        if (last_char == EOF) {
            return TOK_EOF;
        }

        // we found some other ascii symbol and will just return that as an int
        return_char = last_char;
        last_char = getchar();
        return (int)return_char;
    }
}




