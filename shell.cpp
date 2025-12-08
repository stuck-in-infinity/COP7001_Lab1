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

// SIGNAL HANDLER

void sigchld_handler(int)
{
    int saved = errno;
    while (true)
    {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;
    }
    errno = saved;
}

void setup_signal_handlers()
{
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);

    // Shell should ignore Ctrl-C
    signal(SIGINT, SIG_IGN);
}

// TOKENIZER WITH ERROR HANDLING

pair<vector<string>, string> tokenize(const string &line)
{
    vector<string> tokens;
    string cur;
    bool in_quote = false;

    for (size_t i = 0; i < line.size(); i++)
    {
        char c = line[i];

        if (c == '"')
        {
            in_quote = !in_quote;
            continue;
        }

        if (!in_quote && isspace((unsigned char)c))
        {
            if (!cur.empty())
            {
                tokens.push_back(cur);
                cur.clear();
            }
        }
        else
        {
            cur.push_back(c);
        }
    }

    if (!cur.empty())
        tokens.push_back(cur);

    if (in_quote)
    {
        return make_pair(vector<string>(), "Error: Unterminated quote");
    }

    return make_pair(tokens, "");
}

// REDIRECTION VALIDATION
bool isOperator(const string &s)
{
    return (s == "<" || s == ">" || s == "|" || s == "&");
}

string validate_redirection(const vector<string> &tokens)
{
    for (size_t i = 0; i < tokens.size(); i++)
    {
        if (tokens[i] == "<" || tokens[i] == ">")
        {
            if (i + 1 >= tokens.size())
            {
                return "Error: " + tokens[i] + " operator missing filename";
            }

            if (isOperator(tokens[i + 1]))
            {
                return "Error: " + tokens[i] + " operator followed by another operator";
            }

            if (tokens[i] == "<")
            {
                for (size_t j = i + 2; j < tokens.size(); j++)
                {
                    if (tokens[j] == "<")
                    {
                        return "Error: Multiple input redirections not supported";
                    }
                }
            }
            else if (tokens[i] == ">")
            {
                for (size_t j = i + 2; j < tokens.size(); j++)
                {
                    if (tokens[j] == ">")
                    {
                        return "Error: Multiple output redirections not supported";
                    }
                }
            }
        }
    }
    return "";
}

// PIPE PARSING

pair<vector<string>, vector<string>> split_pipe(const vector<string> &tokens)
{
    vector<string> leftCmd, rightCmd;
    bool pipeSeen = false;

    for (const string &tok : tokens)
    {
        if (tok == "|")
        {
            if (pipeSeen)
            {
                cerr << "Error: Multiple pipes not supported\n";
                return {{}, {}};
            }
            pipeSeen = true;
            continue;
        }

        if (!pipeSeen)
            leftCmd.push_back(tok);
        else
            rightCmd.push_back(tok);
    }

    return {leftCmd, rightCmd};
}
string trim(const string &s)
{
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\n\r");
    return s.substr(a, b - a + 1);
}

int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    setup_signal_handlers();

    string line;
    string prompt = "mysh> ";

    while (true)
    {
        // showing prompt and flush asap
        cout << prompt << flush;

        if (!getline(cin, line))
        {
            cout << "\n";
            break;
        }

        // triming leading/trailing spaces
        string trimmed = trim(line);
        if (trimmed.empty())
            continue;

        // token creation
        auto [toks, tok_error] = tokenize(trimmed);
        if (!tok_error.empty())
        {
            cerr << tok_error << "\n";
            continue;
        }

        if (toks.empty())
            continue;

        // background job detection
        bool background = false;
        if (!toks.empty() && toks.back() == "&")
        {
            background = true;
            toks.pop_back();
            if (toks.empty())
                continue;
        }
        string redir_error = validate_redirection(toks);
        if (!redir_error.empty())
        {
            cerr << redir_error << "\n";
            continue;
        }
        auto [cmd1, cmd2] = split_pipe(toks);

        if (cmd1.empty() && !toks.empty())
        {
            continue;
        }

        if (cmd2.empty())
        {
            cmd2 = cmd1;
        }

        bool has_pipe = (cmd2 != cmd1);

        // cerr << "[DEBUG] Input: " << trimmed << "\n";
        // cerr << "[DEBUG] Has pipe: " << (has_pipe ? "YES" : "NO") << "\n";
        // cerr << "[DEBUG] Background: " << (background ? "YES" : "NO") << "\n";
        if (toks[0] == "exit")
        {
            int code = 0;
            if (toks.size() > 1)
            {
                code = stoi(toks[1]);
            }
            return code;
        }

        if (toks[0] == "cd")
        {
            const char *path;
            if (toks.size() > 1)
                path = toks[1].c_str();
            else
                path = getenv("HOME");

            if (chdir(path) != 0)
            {
                perror("cd");
            }
            continue;
        }

        if (!has_pipe)
        {
            string input_file = "";
            string output_file = "";
            vector<string> cleaned;

            for (int i = 0; i < (int)cmd1.size(); i++)
            {
                if (cmd1[i] == "<" && i + 1 < (int)cmd1.size())
                {
                    input_file = cmd1[i + 1];
                    i++;
                }
                else if (cmd1[i] == ">" && i + 1 < (int)cmd1.size())
                {
                    output_file = cmd1[i + 1];
                    i++;
                }
                else
                {
                    cleaned.push_back(cmd1[i]);
                }
            }

            cmd1 = cleaned;
            if (cmd1.empty())
                continue;

            vector<char *> argv;
            vector<string> storage;
            storage.reserve(cmd1.size());
            for (auto &s : cmd1)
                storage.push_back(s);
            for (auto &s : storage)
                argv.push_back(&s[0]);
            argv.push_back(nullptr);

            pid_t pid = fork();
            if (pid < 0)
            {
                perror("fork");
                continue;
            }

            if (pid == 0)
            {
                signal(SIGINT, SIG_DFL);

                if (!input_file.empty())
                {
                    int fd = open(input_file.c_str(), O_RDONLY);
                    if (fd < 0)
                    {
                        perror("input redirection");
                        _exit(1);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }

                if (!output_file.empty())
                {
                    int fd = open(output_file.c_str(),
                                  O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0)
                    {
                        perror("output redirection");
                        _exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }

                execvp(argv[0], argv.data());
                perror("execvp");
                _exit(1);
            }
            else
            {
                if (!background)
                {
                    int status = 0;
                    waitpid(pid, &status, 0);
                }
                else
                {
                    cout << "[background pid " << pid << "]\n";
                }
            }
        }
        else
        {
            if (cmd1.empty() || cmd2.empty())
            {
                cerr << "Error: Pipe syntax requires two commands\n";
                continue;
            }

            int fds[2];
            if (pipe(fds) < 0)
            {
                perror("pipe");
                continue;
            }

            string input_file = "";
            vector<string> cmd1_clean;
            for (int i = 0; i < (int)cmd1.size(); i++)
            {
                if (cmd1[i] == "<" && i + 1 < (int)cmd1.size())
                {
                    input_file = cmd1[i + 1];
                    i++;
                }
                else if (cmd1[i] != ">" && cmd1[i] != "|")
                {
                    cmd1_clean.push_back(cmd1[i]);
                }
            }
            cmd1 = cmd1_clean;

            string output_file = "";
            vector<string> cmd2_clean;
            for (int i = 0; i < (int)cmd2.size(); i++)
            {
                if (cmd2[i] == ">" && i + 1 < (int)cmd2.size())
                {
                    output_file = cmd2[i + 1];
                    i++;
                }
                else if (cmd2[i] != "<" && cmd2[i] != "|")
                {
                    cmd2_clean.push_back(cmd2[i]);
                }
            }
            cmd2 = cmd2_clean;

            if (cmd1.empty() || cmd2.empty())
            {
                cerr << "Error: Pipe commands cannot be empty\n";
                close(fds[0]);
                close(fds[1]);
                continue;
            }

            pid_t pid1 = fork();
            if (pid1 < 0)
            {
                perror("fork");
                close(fds[0]);
                close(fds[1]);
                continue;
            }

            if (pid1 == 0)
            {
                signal(SIGINT, SIG_DFL);

                dup2(fds[1], STDOUT_FILENO);
                close(fds[0]);
                close(fds[1]);

                if (!input_file.empty())
                {
                    int fd = open(input_file.c_str(), O_RDONLY);
                    if (fd < 0)
                    {
                        perror("input redirection");
                        _exit(1);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }

                vector<char *> argv;
                vector<string> storage;
                storage.reserve(cmd1.size());
                for (auto &s : cmd1)
                    storage.push_back(s);
                for (auto &s : storage)
                    argv.push_back(&s[0]);
                argv.push_back(nullptr);

                execvp(argv[0], argv.data());
                perror("execvp");
                _exit(1);
            }

            pid_t pid2 = fork();
            if (pid2 < 0)
            {
                perror("fork");
                close(fds[0]);
                close(fds[1]);
                continue;
            }

            if (pid2 == 0)
            {
                signal(SIGINT, SIG_DFL);

                dup2(fds[0], STDIN_FILENO);
                close(fds[0]);
                close(fds[1]);

                if (!output_file.empty())
                {
                    int fd = open(output_file.c_str(),
                                  O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0)
                    {
                        perror("output redirection");
                        _exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }

                vector<char *> argv;
                vector<string> storage;
                storage.reserve(cmd2.size());
                for (auto &s : cmd2)
                    storage.push_back(s);
                for (auto &s : storage)
                    argv.push_back(&s[0]);
                argv.push_back(nullptr);

                execvp(argv[0], argv.data());
                perror("execvp");
                _exit(1);
            }

            close(fds[0]);
            close(fds[1]);

            if (!background)
            {
                int status1, status2;
                waitpid(pid1, &status1, 0);
                waitpid(pid2, &status2, 0);
            }
            else
            {
                cout << "[background pipe pids " << pid1 << " " << pid2 << "]\n";
            }
        }
    }

    return 0;
}
