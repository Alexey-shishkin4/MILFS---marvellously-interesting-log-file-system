#include <iostream>
#include <string>
#include <vector>


std::vector<std::string> parseCommand(const std::string& input) {
    std::string token;
    char quote = 0, c = 0;
    bool flag_added = true;
    bool flag_quote = false;
    std::vector<std::string> result; // tokens
    
    for (size_t i = 0; i < input.length(); i++) {
        c = input[i];

        if (c == '"' || c == '\'') {
            if (flag_quote && c != quote) { std::cout << "ERROR qoute!"; exit(1); }
            
            flag_quote = !flag_quote;
            quote = flag_quote ? c : 0;
        
        } else if (!flag_quote && isspace(c)) { 
            flag_added = false; 
            if (!token.empty()) { result.push_back(token); token.clear(); }
        }
        
        if (flag_added) { token += c; }
        flag_added = true;
    }

    if (!token.empty()) { result.push_back(token); }
    return result;
}

int cli_main() {
    std::cout << "Simple CLI gateway. Enter 'exit' to exit.\n";

    while (true) {
        std::cout << "> ";
        std::string input;
        std::getline(std::cin, input);

        if (input.empty()) {
            continue;
        }

        std::vector<std::string> tokens = parseCommand(input);

        if (tokens.empty()) {
            continue;
        }

        std::string command = tokens[0];

        if (command == "exit") {
            std::cout << "Exit to program!\n";
            break;
        }

        std::cout << "command: " << command << std::endl;

        for (size_t i = 1; i < tokens.size(); i++) {
            std::cout << "arg: " << tokens[i] << std::endl;
        }
    }

    return 0;
}