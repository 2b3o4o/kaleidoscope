#include <cstdlib>
#include <cassert>
#include <string>
#include <cctype>
#include <map>
#include <memory>
#include <vector>

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

using namespace llvm;

namespace { // Unnamed namespace: contents are automatically scoped to this file.
    typedef enum Token {
        TOK_EOF = -1,
        TOK_DEF = -2,
        TOK_EXTERN = -3,
        TOK_IDENTIFIER = -4,
        TOK_NUMBER = -5,
    } Token;
    std::string identifier_string;
    double number_val = 69;

    // --- LLVM IR static variables ---
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<IRBuilder<>> builder;
    std::unique_ptr<llvm::Module> module;
    std::map<std::string, Value*> named_values;

    int get_token() {
        static char last_char = ' '; // only executes on first func call.
        while (isspace(last_char)) {
            last_char = getchar();
        }

        // fprintf(stderr, "Read stdin char %c\n", last_char);

        if (isalpha(last_char)) { // we're at the start of a word: either keyword or an identifier
            identifier_string = last_char;
            last_char = getchar();
            while (isalnum(last_char)) {
                identifier_string += last_char;
                last_char = getchar();
            }
            static const std::map<std::string, int> string_to_keyword = {
                {"def", TOK_DEF},
                {"extern", TOK_EXTERN},
            };
            return string_to_keyword.count(identifier_string) > 0 ? string_to_keyword.at(identifier_string) : TOK_IDENTIFIER;
        }

        if (isdigit(last_char)) { // we found a float
            std::string num_string;
            num_string = last_char;
            last_char = getchar();
            while (isdigit(last_char) || last_char == '.') {
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
            if (last_char != EOF) {
                last_char = getchar();
                return get_token();
            }
        }

        if (last_char == EOF) {
            return TOK_EOF;
        }

        // we found some other ascii symbol and will just return that as an int
        char return_char = last_char;
        last_char = getchar();
        return (int)return_char;
    }

    class AstExpr {
        public:
        virtual ~AstExpr() = default;
        virtual Value* codegen() = 0;
    };

    class NumLiteralExpr : public AstExpr {
        public:
        NumLiteralExpr(double val) : value(val) {};
        Value* codegen()  override {
            return ConstantFP::get(*context, APFloat(this->value));
        };

        private:
        double value;
    };

    class VarExpr : public AstExpr {
        public:
        VarExpr(const std::string& name)
            : name(name) {}
        Value* codegen() override {
            Value* val = named_values[this->name];
            if (!val) {
                return log_error_val(("Attempted access of undefined variable " + this->name).c_str());
            }
            return val;
        }

        private:
        std::string name;
    };

    class BinaryExpr : public AstExpr {
        public:
        BinaryExpr(char kld_op, std::unique_ptr<AstExpr> lhs, std::unique_ptr<AstExpr> rhs)
            : kld_op(kld_op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
        
        Value* codegen() override {
            Value* left = lhs->codegen();
            Value* right = rhs->codegen();

            if (!left || !right) {
                return nullptr;
            }

            switch (kld_op) {
                case '+':
                    return builder->CreateFAdd(left, right, "addtmp");
                case '*':
                    return builder->CreateFMul(left, right, "multmp");
                case '-':
                    return builder->CreateFSub(left, right, "subtmp");
                case '<':
                    left = builder->CreateFCmpULT(left, right, "cmptmp");
                    return builder->CreateUIToFP(left, Type::getDoubleTy(context), "booltmp");
                default:
                    return log_error_val(("Invalid binary operator '" + kld_op + '\'').c_str());
            }
        }
        
        private:
        char kld_op;
        std::unique_ptr<AstExpr> lhs, rhs;
    };

    class CallExpr : public AstExpr {
        public:
        CallExpr(const std::string& callee_name, std::vector<std::unique_ptr<AstExpr>> args)
            : callee_name(callee_name), args(std::move(args)) {}

        Value* codegen() override {
            Function* callee_func = module->getFunction(callee_name);
            if (!callee_func) {
                return log_error_val("Unknown function being called");
            }
            if (callee_func->arg_size() != args.size()) {
                return log_error_val("Wrong number of arguments provided in function call");
            }

            std::vector<Value*> arg_vals;
            for (auto arg : args) {
                arg_vals.push_back(arg);
                if (!arg_vals.back()) {
                    return nullptr;
                }
            }
            return builder->CreateCall(callee_func, arg_vals, "calltmp");
        }

        private:
        std::string callee_name;
        std::vector<std::unique_ptr<AstExpr>> args;
    };

    class AstPrototype {
        public:
        AstPrototype(const std::string& name, std::vector<std::string> arg_names)
            : name(name), arg_names(arg_names) {}

        const std::string& get_name() const {
            return name;
        }

        Function* codegen() override {
            std::vector<Type*> doubles(arg_names.size(), Type::getDoubleTy(context));
            FunctionType* type = FunctionType::get(Type::getDoubleTy(context), doubles, false);
            Function* func = Function::Create(type, Function::ExternalLinkage, name, module.get());

            // set arg names
            int size = func->args().size();
            for (int i = 0; i < size; i++) {
                func->args()[i].setName(arg_names[i]);
            }
            return func;
        }

        private:
        std::string name;
        std::vector<std::string> arg_names;
    };

    class AstFuncDef {
        public:
        AstFuncDef(std::unique_ptr<AstPrototype> prototype, std::unique_ptr<AstExpr> body)
            : prototype(std::move(prototype)), body(std::move(body)) {}

        Function* codegen() override {
            Function* func = module.getFunction(proto->get_name());

            if (!func) {
                func = proto.codegen();
            }
            if (!func) {
                return nullptr;
            }
            if (!func.empty()) {
                return (Function*)log_error_val("Function body already defined.");
            }

            BasicBlock* block = BasicBlock::Create(*context, "entry", func);
            builder->SetInsertPoint(block);

            named_values.clear();
            for (auto &arg : func.args()) {
                named_values[std::string(arg.getName)] = &arg;
            }

            Value* body_retval = body->codegen();
            if (!body_retval) {
                func.eraseFromParent();
            }
            builder->CreateRet(body_retval);
            verifyFunction(*func);
            return func;
        }

        private:
        std::unique_ptr<AstPrototype> prototype;
        std::unique_ptr<AstExpr> body;
    };


    // --- Parsing Helpers ---
    std::unique_ptr<AstExpr> parse_expr();

    int curr_token = -69;
    int get_next_token() {
        curr_token = get_token();
        return curr_token;
    }

    void log_error(const char* const msg) {
        fprintf(stderr, "Error: %s\n", msg);
    }

    std::unique_ptr<AstExpr> log_error_expr(const char* const msg) {
        log_error(msg);
        return nullptr;
    }

    std::unique_ptr<AstPrototype> log_error_proto(const char* const msg) {
        log_error(msg);
        return nullptr;
    }

    llvm::Value* log_error_val(const char* const msg) {
        log_error(msg);
        return nullptr;
    }

    // --- AstExpr Generators ---
    auto parse_number() {
        assert(curr_token == TOK_NUMBER);
        auto expr = std::make_unique<NumLiteralExpr>(number_val);
        get_next_token();
        return expr;
    }

    std::unique_ptr<AstExpr> parse_paren_expr() {
        assert(curr_token == '(');
        get_next_token();
        auto expr = parse_expr();
        if (!expr) {
            return nullptr;
        }
        if (curr_token != ')') {
            return log_error_expr("Expected ')'");
        }
        get_next_token();
        return expr;
    }

    std::unique_ptr<AstExpr> parse_identifier() {
        assert(curr_token == TOK_IDENTIFIER);
        std::string id_name = identifier_string; // identifier_string may be overridden by next call
        get_next_token();

        if (curr_token != '(') { // this token must be a variable
            return std::make_unique<VarExpr>(id_name);
        }

        // otherwise this must be a function invocation
        get_next_token(); // discard '('
        auto args = std::vector<std::unique_ptr<AstExpr>>();
        if (curr_token != ')') { // if we do find ')' here that just means the function call has no params
            while (true) {
                auto arg = parse_expr();
                if (!arg) {
                    return nullptr;
                }
                args.push_back(move(arg));
                if (curr_token == ')') { // finished parsing args
                    break;
                }
                if (curr_token != ',') {
                    return log_error_expr("Expected ',' or ')'");
                }
            }
        }
        get_next_token(); // discard ')'
        auto call_expr = std::make_unique<CallExpr>(id_name, std::move(args));
        return call_expr;
    }

    std::unique_ptr<AstExpr> parse_primary() {
        switch (curr_token) {
            case TOK_IDENTIFIER:
                return parse_identifier();
            case TOK_NUMBER:
                return parse_number();
            case '(':
                return parse_paren_expr();
            default:
                return log_error_expr(("Unexpected token with id " + std::to_string(curr_token)).c_str());
        }
    }

    // --- Binary operator parsing and precedence definitions ---
    int get_binary_precedence(char op) {
        switch (op) {
            case '<':
                return 10;
            case '+':
            case '-':
                return 20;
            case '*':
                return 40;
            default:
                return -1;
        }
    }

    std::unique_ptr<AstExpr> parse_bin_op_rhs(int lhs_prec, std::unique_ptr<AstExpr> lhs) {
        // so basically we need to call this recursively, until the next call says it can't run because its precedence is lower than
        // the precedence of this call's operator. only then can we evaluate the current BinaryExpr and return it.
        while (true) {
            int curr_prec = get_binary_precedence(curr_token);

            if (curr_prec < lhs_prec) { // checks if the preceding expr should be evaluated before this one.
                return lhs;           // on subsequent iterations though, I think maybe we need to update my_prec but currently don't
            }

            char op = curr_token;
            get_next_token(); // eat operator

            auto rhs = parse_primary();
            if (!rhs) {
                return nullptr;
            }

            // at this point we've already eaten the RHS primary expression, so we can look ahead to the next binary operator to compare its precedence to this one's
            int next_prec = get_binary_precedence(curr_token);
            if (curr_prec < next_prec) {
                // TODO: we need to evaluate the next binary expression first before making it the rhs for this one
                rhs = parse_bin_op_rhs(curr_prec + 1, move(rhs)); // TODO: I don't see why the + 1 would be important so I should try debugging this without it
                if (!rhs) {
                    return nullptr;
                }
            }

            lhs = std::make_unique<BinaryExpr>(op, move(lhs), move(rhs));
        }
    }
    
    /**
     * An expression consists of an LHS primary expression, optionally followed by a binary operator and an RHS primary expression.
    */
    std::unique_ptr<AstExpr> parse_expr() {
        auto lhs = parse_primary();
        if (!lhs) {
            return nullptr;
        }
        return parse_bin_op_rhs(0, move(lhs));
    }

    std::unique_ptr<AstPrototype> parse_prototype() {
        assert(curr_token == TOK_IDENTIFIER);
        std::string func_name = identifier_string;
        get_next_token();

        if (curr_token != '(') {
            return log_error_proto("Expected '('");
        }
        get_next_token();

        auto args = std::vector<std::string>();
        while (curr_token == TOK_IDENTIFIER) {
            args.push_back(identifier_string);
            get_next_token();
        }

        if (curr_token != ')') {
            return log_error_proto("Expected '('");
        }
        get_next_token();

        return std::make_unique<AstPrototype>(func_name, move(args));
    }

    std::unique_ptr<AstFuncDef> parse_definition() {
        assert(curr_token == TOK_DEF);
        get_next_token();

        auto proto = parse_prototype();
        if (!proto) {
            return nullptr;
        }

        auto body = parse_expr();
        if (!body) {
            return nullptr;
        }

        return std::make_unique<AstFuncDef>(move(proto), move(body));
    }

    std::unique_ptr<AstPrototype> parse_extern() {
        assert(curr_token == TOK_EXTERN);
        get_next_token();

        return parse_prototype();
    }

    /**
     * We handle top level expressions by parsing them as anonymous (unnamed) unary (no argument) functions
    */
    std::unique_ptr<AstFuncDef> parse_top_level_expr() {
        auto expr = parse_expr();
        if (!expr) {
            return nullptr;
        }

        auto proto = std::make_unique<AstPrototype>("", std::vector<std::string>());
        assert(proto);
        return std::make_unique<AstFuncDef>(std::move(proto), move(expr));
    }



    // --- Handlers for main loop, for now they just print what they found
    void handle_definition() {
        auto def = parse_definition();
        if (!def) {
            // error should be printed by parsing logic, but we'll skip the token we failed on
            get_next_token();
            return;
        }
        fprintf(stderr, "Found a definition!\n");
    }

    void handle_extern() {
        auto decl = parse_extern();
        if (!decl) {
            // error should be printed by parsing logic, but we'll skip the token we failed on
            get_next_token();
            return;
        }
        fprintf(stderr, "Found a extern!\n");
    }

    void handle_top_level_expr() {
        auto expr = parse_top_level_expr();
        if (!expr) {
            // error should be printed by parsing logic, but we'll skip the token we failed on
            get_next_token();
            return;
        }
        fprintf(stderr, "Found a top level expression!\n");
    }
}

int main() {
    get_next_token();
    while (true) {
        switch (curr_token) {
            case TOK_EOF:
                return 0;
            case ';':
                get_next_token(); // semicolons BTFO
                break;
            case TOK_DEF:
                handle_definition();
                break;
            case TOK_EXTERN:
                handle_extern();
                break;
            default:
                handle_top_level_expr();
                break;
        }
    }
}


// int main() {
//     // fprintf(stderr, "ready> ");
//     // get_next_token();
//     bool once = true;
//     while (true) {
//         fprintf(stderr, "ready> ");
//         if (once) {get_next_token(); once = false; }
//         switch (curr_token) {
//             case TOK_EOF:
//                 return 0;
//             case ';':
//                 get_next_token(); // semicolons BTFO
//                 break;
//             case TOK_DEF:
//                 handle_definition();
//                 break;
//             case TOK_EXTERN:
//                 handle_extern();
//                 break;
//             default:
//                 handle_top_level_expr();
//                 break;
//         }
//     }
// }



