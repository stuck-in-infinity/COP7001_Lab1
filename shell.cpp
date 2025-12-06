// src/shell.cpp
// Mini Shell: Commands, Quoting, Background, Signals, Redirection (<, >)

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <cstdlib>
#include <cctype>

using namespace std;

// ============== SIGNAL HANDLERS ==============

void sigchld_handler(int sig) {
    int saved = errno;
    while (true) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;
    }
    errno = saved;
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);
    
    // Shell should ignore Ctrl-C
    signal(SIGINT, SIG_IGN);
}

// ============== TOKENIZER WITH ERROR HANDLING ==============

// Returns pair<tokens, error_message>
// If error_message is not empty, tokenization failed
pair<vector<string>, string> tokenize(const string &line) {
    vector<string> tokens;
    string cur;
    bool in_quote = false;
    
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        
        if (c == '"') {
            in_quote = !in_quote;
            continue;
        }
        
        if (!in_quote && isspace((unsigned char)c)) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    
    if (!cur.empty())
        tokens.push_back(cur);
    
    // CHECK FOR UNTERMINATED QUOTE
    if (in_quote) {
        return make_pair(vector<string>(), "Error: Unterminated quote");
    }
    
    return make_pair(tokens, "");
}

// ============== REDIRECTION VALIDATION ==============

string validate_redirection(const vector<string> &tokens) {
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "<" || tokens[i] == ">") {
            if (i + 1 >= tokens.size()) {
                return "Error: " + tokens[i] + " operator missing filename";
            }
            
            if (tokens[i + 1] == "<" || tokens[i + 1] == ">" || tokens[i + 1] == "|" || tokens[i + 1] == "&") {
                return "Error: " + tokens[i] + " operator followed by another operator";
            }
            
            if (tokens[i] == "<") {
                for (size_t j = i + 2; j < tokens.size(); j++) {
                    if (tokens[j] == "<") {
                        return "Error: Multiple input redirections not supported";
                    }
                }
            } else if (tokens[i] == ">") {
                for (size_t j = i + 2; j < tokens.size(); j++) {
                    if (tokens[j] == ">") {
                        return "Error: Multiple output redirections not supported";
                    }
                }
            }
        }
    }
    return "";
}

// ============== PIPE PARSING ==============

pair<vector<string>, vector<string>> split_pipe(const vector<string> &tokens) {
    vector<string> cmd1, cmd2;
    bool found_pipe = false;
    
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "|") {
            if (found_pipe) {
                cerr << "Error: Multiple pipes not supported\n";
                return make_pair(vector<string>(), vector<string>());
            }
            found_pipe = true;
            continue;
        }
        
        if (!found_pipe)
            cmd1.push_back(tokens[i]);
        else
            cmd2.push_back(tokens[i]);
    }
    
    return make_pair(cmd1, cmd2);
}

// =============================================================

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    
    setup_signal_handlers();
    
    string line;
    string prompt = "mysh> ";
    
    while (true) {
        // show prompt and flush immediately
        cout << prompt << flush;
        
        if (!getline(cin, line)) {
            cout << "\n";
            break;
        }
        
        // trim leading/trailing spaces
        auto l = line.find_first_not_of(" \t\r\n");
        if (l == string::npos)
            continue;
        auto r = line.find_last_not_of(" \t\r\n");
        string trimmed = line.substr(l, r - l + 1);
        
        // ============ TOKENIZE WITH ERROR CHECKING ============
        auto [toks, tok_error] = tokenize(trimmed);
        if (!tok_error.empty()) {
            cerr << tok_error << "\n";
            continue;
        }
        
        if (toks.empty())
            continue;
        
        // detect background job
        bool background = false;
        if (!toks.empty() && toks.back() == "&") {
            background = true;
            toks.pop_back();
            if (toks.empty())
                continue;
        }
        
        // ============ VALIDATE REDIRECTION SYNTAX ============
        string redir_error = validate_redirection(toks);
        if (!redir_error.empty()) {
            cerr << redir_error << "\n";
            continue;
        }
        
        // ============ CHECK FOR PIPE ============
        auto [cmd1, cmd2] = split_pipe(toks);
        
        if (cmd1.empty() && !toks.empty()) {
            continue;
        }
        
        if (cmd2.empty()) {
            cmd2 = cmd1;
        }
        
        bool has_pipe = (cmd2 != cmd1);
        
        // ============ BUILT-INS ============
        
        if (toks[0] == "exit") {
            int code = 0;
            if (toks.size() > 1) {
                code = stoi(toks[1]);
            }
            return code;
        }
        
        if (toks[0] == "cd") {
            const char *path;
            if (toks.size() > 1)
                path = toks[1].c_str();
            else
                path = getenv("HOME");
            
            if (chdir(path) != 0) {
                perror("cd");
            }
            continue;
        }
        
        // ============ PARSE REDIRECTION ============
        
        string input_file = "";
        string output_file = "";
        vector<string> cleaned;
        
        for (int i = 0; i < (int)toks.size(); i++) {
            if (toks[i] == "<" && i + 1 < (int)toks.size()) {
                input_file = toks[i + 1];
                i++;
            } else if (toks[i] == ">" && i + 1 < (int)toks.size()) {
                output_file = toks[i + 1];
                i++;
            } else {
                cleaned.push_back(toks[i]);
            }
        }
        
        toks = cleaned;
        if (toks.empty())
            continue;
        
        // Prepare argv for execvp
        vector<char*> argv;
        vector<string> storage;
        storage.reserve(toks.size());
        for (auto &s : toks)
            storage.push_back(s);
        for (auto &s : storage)
            argv.push_back(&s[0]);
        argv.push_back(nullptr);
        
        // ============ FORK AND EXECUTE ============
        
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;
        }
        
        if (pid == 0) {
            // CHILD PROCESS
            signal(SIGINT, SIG_DFL);  // child should die on Ctrl-C
            
            // ----- INPUT REDIRECTION -----
            if (!input_file.empty()) {
                int fd = open(input_file.c_str(), O_RDONLY);
                if (fd < 0) {
                    perror("input redirection");
                    _exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            
            // ----- OUTPUT REDIRECTION -----
            if (!output_file.empty()) {
                int fd = open(output_file.c_str(),
                              O_WRONLY | O_CREAT | O_TRUNC,
                              0644);
                if (fd < 0) {
                    perror("output redirection");
                    _exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            
            // execute command
            execvp(argv[0], argv.data());
            perror("execvp");
            _exit(1);
            
        } else {
            // PARENT PROCESS
            if (!background) {
                int status = 0;
                waitpid(pid, &status, 0);
            } else {
                cout << "[background pid " << pid << "]\n";
            }
        }
    }
    
    return 0;
}



