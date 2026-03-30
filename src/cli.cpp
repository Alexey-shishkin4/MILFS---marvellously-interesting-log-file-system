#include "headers/fs_core.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static FileSystemState g_fs;

std::vector<std::string> parseCommand(const std::string& input) {
    std::string token;
    char c = 0;
    char quote = 0;
    bool in_quote = false;
    std::vector<std::string> result;

    for (size_t i = 0; i < input.length(); i++) {
        c = input[i];

        if (c == '"' || c == '\'') {
            if (in_quote && c == quote) {
                in_quote = false;
                quote = 0;
            } else if (!in_quote) {
                in_quote = true;
                quote = c;
            } else {
                token += c;
            }
            continue;
        }

        if (!in_quote && std::isspace(static_cast<unsigned char>(c))) {
            if (!token.empty()) {
                result.push_back(token);
                token.clear();
            }
            continue;
        }

        token += c;
    }

    if (!token.empty()) {
        result.push_back(token);
    }

    return result;
}

void printStatus(FsError err) {
    switch (err) {
        case FsError::Ok:
            std::cout << "ok\n";
            break;
        case FsError::NotFound:
            std::cout << "error: not found\n";
            break;
        case FsError::AlreadyExists:
            std::cout << "error: already exists\n";
            break;
        case FsError::NotDirectory:
            std::cout << "error: not a directory\n";
            break;
        case FsError::IsDirectory:
            std::cout << "error: is a directory\n";
            break;
        case FsError::InvalidPath:
            std::cout << "error: invalid path\n";
            break;
        case FsError::NoSpace:
            std::cout << "error: no space\n";
            break;
        case FsError::Internal:
        default:
            std::cout << "error: internal error\n";
            break;
    }
}

void printHelp() {
    std::cout
        << "Available commands:\n"
        << "  help                      - show this help\n"
        << "  mkdir <path>              - create directory\n"
        << "  create <path>             - create empty file\n"
        << "  write <path> <data>       - write data to file\n"
        << "  cat <path>                - print file contents\n"
        << "  read <path>               - same as cat\n"
        << "  ls [path]                 - list directory contents (default: /)\n"
        << "  exit                      - exit program\n";
}

int cli_main() {
    fs_init(g_fs);

    std::cout << "MILFS CLI gateway. Enter 'help' for commands, 'exit' to quit.\n";

    while (true) {
        std::cout << "> ";

        std::string input;
        if (!std::getline(std::cin, input)) {
            std::cout << "\nExit from program!\n";
            break;
        }

        if (input.empty()) {
            continue;
        }

        std::vector<std::string> tokens = parseCommand(input);

        if (tokens.empty()) {
            continue;
        }

        const std::string& command = tokens[0];

        if (command == "exit") {
            std::cout << "Exit from program!\n";
            break;
        }

        if (command == "help") {
            printHelp();
            continue;
        }

        if (command == "mkdir") {
            if (tokens.size() != 2) {
                std::cout << "usage: mkdir <path>\n";
                continue;
            }

            printStatus(fs_mkdir(g_fs, tokens[1]));
            continue;
        }

        if (command == "create") {
            if (tokens.size() != 2) {
                std::cout << "usage: create <path>\n";
                continue;
            }

            printStatus(fs_create(g_fs, tokens[1]));
            continue;
        }

        if (command == "write") {
            if (tokens.size() < 3) {
                std::cout << "usage: write <path> <data>\n";
                continue;
            }

            std::string data;
            for (size_t i = 2; i < tokens.size(); ++i) {
                if (i > 2) {
                    data += " ";
                }
                data += tokens[i];
            }

            printStatus(fs_write(g_fs, tokens[1], data));
            continue;
        }

        if (command == "cat" || command == "read") {
            if (tokens.size() != 2) {
                std::cout << "usage: cat <path>\n";
                continue;
            }

            std::string out;
            FsError err = fs_read(g_fs, tokens[1], out);
            if (err == FsError::Ok) {
                std::cout << out << '\n';
            } else {
                printStatus(err);
            }
            continue;
        }

        if (command == "ls") {
            std::string path = "/";
            if (tokens.size() == 2) {
                path = tokens[1];
            } else if (tokens.size() > 2) {
                std::cout << "usage: ls [path]\n";
                continue;
            }

            std::vector<std::string> entries;
            FsError err = fs_listdir(g_fs, path, entries);
            if (err != FsError::Ok) {
                printStatus(err);
                continue;
            }

            for (const auto& name : entries) {
                std::cout << name << '\n';
            }
            if (entries.empty()) {
                std::cout << "(empty)\n";
            }
            continue;
        }

        std::cout << "Unknown command: " << command << '\n';
    }

    return 0;
}

