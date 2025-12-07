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

// Reaps zombie processes asynchronously
void sigchld_handler(int sig) {
    int saved_errno = errno;
    while (waitpid(-1, nullptr, WNOHANG) > 0);
    errno = saved_errno;
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);
    
    // Shell ignores Ctrl-C; child processes will default it back
    signal(SIGINT, SIG_IGN);
}

// ============== TOKENIZER ==============

// Splits line into tokens, handling quotes and special characters (<, >, |, &)
pair<vector<string>, string> tokenize(const string &line) {
    vector<string> tokens;
    string cur;
    bool in_quote = false;
    
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        
        if (c == '"') {
            in_quote =!in_quote;
            continue;
        }
        
        if (in_quote) {
            cur.push_back(c);
            continue;
        }
        
        // Handle special delimiters outside quotes
        if (c == '<' |

| c == '>' |
| c == '|' |
| c == '&') {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
            string op(1, c);
            tokens.push_back(op);
        } else if (isspace((unsigned char)c)) {
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
    
    if (in_quote) {
        return { {}, "Error: Unterminated quote" };
    }
    
    return { tokens, "" };
}

// ============== VALIDATION ==============

string validate_redirection(const vector<string> &tokens) {
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "<" |

| tokens[i] == ">") {
            if (i + 1 >= tokens.size()) {
                return "Error: " + tokens[i] + " operator missing filename";
            }
            if (tokens[i + 1] == "<" |

| tokens[i + 1] == ">" |
| 
                tokens[i + 1] == "|" |

| tokens[i + 1] == "&") {
                return "Error: " + tokens[i] + " operator followed by another operator";
            }
        }
    }
    return "";
}

// ============== PIPE SPLITTING ==============

pair<vector<string>, vector<string>> split_pipe(const vector<string> &tokens) {
    vector<string> cmd1, cmd2;
    bool found_pipe = false;
    
    for (const auto &t : tokens) {
        if (t == "|") {
            if (found_pipe) return { {}, {} }; // Multiple pipes not supported in this basic version
            found_pipe = true;
            continue;
        }
        if (!found_pipe) cmd1.push_back(t);
        else cmd2.push_back(t);
    }
    
    return { cmd1, cmd2 };
}

// ============== HELPER: VECTOR TO CHAR* ARRAY ==============

vector<char*> vec_to_char_array(vector<string> &args) {
    vector<char*> argv;
    for (auto &s : args) {
        argv.push_back(&s);
    }
    argv.push_back(nullptr);
    return argv;
}

// =============================================================

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    
    setup_signal_handlers();
    
    string line;
    string prompt = "myshell> ";
    
    while (true) {
        cout << prompt << flush;
        
        if (!getline(cin, line)) {
            cout << "\n";
            break; 
        }
        
        // Tokenize
        auto [toks, tok_error] = tokenize(line);
        if (!tok_error.empty()) {
            cerr << tok_error << "\n";
            continue;
        }
        if (toks.empty()) continue;
        
        // Check Background
        bool background = false;
        if (toks.back() == "&") {
            background = true;
            toks.pop_back();
            if (toks.empty()) continue;
        }
        
        // Validate Syntax
        string redir_error = validate_redirection(toks);
        if (!redir_error.empty()) {
            cerr << redir_error << "\n";
            continue;
        }
        
        // Check Pipe
        auto [cmd1_full, cmd2_full] = split_pipe(toks);
        bool has_pipe =!cmd2_full.empty();
        
        // ============== BUILT-INS ==============
        
        // Fix: Check empty and use index 0
        if (!cmd1_full.empty() && cmd1_full == "exit" &&!has_pipe) {
            return 0;
        }
        
        if (!cmd1_full.empty() && cmd1_full == "cd" &&!has_pipe) {
            const char *path;
            if (cmd1_full.size() > 1) path = cmd1_full.[1]c_str();
            else path = getenv("HOME");
            
            if (chdir(path)!= 0) perror("cd");
            continue;
        }
        
        // ============== EXECUTION ==============
        
        if (!has_pipe) {
            // Single Command Logic
            vector<string> cmd;
            string input_file, output_file;
            
            for (size_t i = 0; i < cmd1_full.size(); i++) {
                if (cmd1_full[i] == "<") {
                    input_file = cmd1_full[++i];
                } else if (cmd1_full[i] == ">") {
                    output_file = cmd1_full[++i];
                } else {
                    cmd.push_back(cmd1_full[i]);
                }
            }
            
            if (cmd.empty()) continue;
            
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                continue;
            }
            
            if (pid == 0) {
                // Child
                signal(SIGINT, SIG_DFL); // Restore Ctrl-C
                
                if (!input_file.empty()) {
                    int fd = open(input_file.c_str(), O_RDONLY);
                    if (fd < 0) { perror("input redirect"); _exit(1); }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
                
                if (!output_file.empty()) {
                    int fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0) { perror("output redirect"); _exit(1); }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                
                vector<char*> argv = vec_to_char_array(cmd);
                execvp(argv, argv.data());
                perror("execvp");
                _exit(1);
            }
            
            // Parent
            if (!background) {
                waitpid(pid, nullptr, 0);
            } else {
                cout << "[pid " << pid << "]\n";
            }
            
        } else {
            // Pipeline Logic
            int fds[2];
            if (pipe(fds) < 0) { perror("pipe"); continue; }
            
            // Parse Left Command (cmd1)
            vector<string> c1;
            string c1_in;
            for (size_t i = 0; i < cmd1_full.size(); i++) {
                if (cmd1_full[i] == "<") c1_in = cmd1_full[++i];
                else if (cmd1_full[i] == ">") i++; // Ignore output redirection in LHS of pipe
                else c1.push_back(cmd1_full[i]);
            }
            
            // Parse Right Command (cmd2)
            vector<string> c2;
            string c2_out;
            for (size_t i = 0; i < cmd2_full.size(); i++) {
                if (cmd2_full[i] == ">") c2_out = cmd2_full[++i];
                else if (cmd2_full[i] == "<") i++; // Ignore input redirection in RHS of pipe
                else c2.push_back(cmd2_full[i]);
            }
            
            if (c1.empty() |

| c2.empty()) {
                cerr << "Invalid pipe syntax\n";
                close(fds); close(fds[1]);
                continue;
            }

            // Fork Left
            pid_t p1 = fork();
            if (p1 == 0) {
                signal(SIGINT, SIG_DFL);
                if (!c1_in.empty()) {
                    int fd = open(c1_in.c_str(), O_RDONLY);
                    if (fd < 0) { perror("input redirect"); _exit(1); }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
                dup2(fds[1], STDOUT_FILENO); // Output to pipe
                close(fds);
                close(fds[1]);
                
                vector<char*> argv = vec_to_char_array(c1);
                execvp(argv, argv.data());
                perror("execvp c1");
                _exit(1);
            }
            
            // Fork Right
            pid_t p2 = fork();
            if (p2 == 0) {
                signal(SIGINT, SIG_DFL);
                if (!c2_out.empty()) {
                    int fd = open(c2_out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0) { perror("output redirect"); _exit(1); }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                dup2(fds, STDIN_FILENO); // Input from pipe
                close(fds);
                close(fds[1]);
                
                vector<char*> argv = vec_to_char_array(c2);
                execvp(argv, argv.data());
                perror("execvp c2");
                _exit(1);
            }
            
            // Parent Close Pipe
            close(fds);
            close(fds[1]);
            
            if (!background) {
                waitpid(p1, nullptr, 0);
                waitpid(p2, nullptr, 0);
            } else {
                cout << "[pids " << p1 << " " << p2 << "]\n";
            }
        }
    }
    return 0;
}




