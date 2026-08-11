#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>

using namespace std;

// Access the global environment variables array
extern char **environ;

// Function to handle the 'help' command using the 'more' filter
void print_help() {
    int pipefd[2];
    pipe(pipefd); // Create a pipe to connect our shell to the 'more' command

    pid_t pid = fork();
    if (pid == 0) {
        // Child process: connect read-end of pipe to Standard Input, then run 'more'
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execlp("more", "more", NULL);
        exit(1);
    } else {
        // Parent process: write the manual to the write-end of the pipe
        close(pipefd[0]);
        string manual = 
            "\n--- MyShell C++ User Manual ---\n"
            "cd [DIR]       : Change directory or print current if no args.\n"
            "dir [DIR]      : List contents of DIRECTORY.\n"
            "environ        : List all environment strings.\n"
            "set VAR VALUE  : Set environment variable VAR to VALUE.\n"
            "echo [COMMENT] : Print COMMENT to screen.\n"
            "help           : Show this manual using 'more'.\n"
            "pause          : Pause shell until Enter is pressed.\n"
            "quit           : Exit the shell.\n"
            "Features       : I/O redirection (<, >, >>), background execution (&), batch scripts.\n\n";
        
        write(pipefd[1], manual.c_str(), manual.length());
        close(pipefd[1]);
        waitpid(pid, NULL, 0); // Wait for user to finish reading
    }
}

// Function to handle all internal built-in commands
// Returns true if a built-in was found and executed, false otherwise
bool execute_builtin(const vector<string>& args) {
    if (args.empty()) return false;

    if (args[0] == "cd") {
        if (args.size() == 1) {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd))) cout << cwd << endl;
        } else {
            if (chdir(args[1].c_str()) != 0) {
                perror("cd error");
            } else {
                // Update PWD environment variable
                char cwd[1024];
                getcwd(cwd, sizeof(cwd));
                setenv("PWD", cwd, 1);
            }
        }
        return true;
    } 
    else if (args[0] == "dir") {
        string target = (args.size() > 1) ? args[1] : ".";
        DIR *d = opendir(target.c_str());
        if (d) {
            struct dirent *dir;
            while ((dir = readdir(d)) != NULL) {
                cout << dir->d_name << endl;
            }
            closedir(d);
        } else {
            perror("dir error");
        }
        return true;
    } 
    else if (args[0] == "environ") {
        for (char **env = environ; *env != 0; env++) {
            cout << *env << endl;
        }
        return true;
    } 
    else if (args[0] == "set") {
        if (args.size() >= 3) {
            setenv(args[1].c_str(), args[2].c_str(), 1);
        } else {
            cout << "Usage: set VARIABLE VALUE" << endl;
        }
        return true;
    } 
    else if (args[0] == "echo") {
        for (size_t i = 1; i < args.size(); ++i) {
            cout << args[i] << " ";
        }
        cout << endl;
        return true;
    } 
    else if (args[0] == "help") {
        print_help();
        return true;
    } 
    else if (args[0] == "pause") {
        cout << "Press 'Enter' to continue..." << endl;
        cin.ignore(10000, '\n'); // Wait for Enter key
        return true;
    } 
    else if (args[0] == "quit") {
        exit(0);
    }
    
    return false; // Not a built-in command
}

int main(int argc, char *argv[]) {
    istream* input_stream = &cin;
    ifstream batch_file;

    // Check if a batch file was provided as an argument
    if (argc == 2) {
        batch_file.open(argv[1]);
        if (!batch_file.is_open()) {
            cerr << "Error opening batch file: " << argv[1] << endl;
            return 1;
        }
        input_stream = &batch_file;
    }

    string line;
    while (true) {
        // Print the prompt only if we are reading from the terminal
        if (input_stream == &cin) {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd))) {
                cout << cwd << "> ";
            } else {
                cout << "myshell> ";
            }
        }

        // Read the full line of input
        if (!getline(*input_stream, line)) {
            break; // Exit if end-of-file is reached
        }

        // Parse the line into a vector of words (tokens)
        // stringstream automatically reduces multiple spaces/tabs into single spaces!
        stringstream ss(line);
        string token;
        vector<string> tokens;
        while (ss >> token) {
            tokens.push_back(token);
        }

        if (tokens.empty()) continue;

        // Variables for redirection and background processing
        string inFile = "", outFile = "";
        bool append = false, isBackground = false;
        vector<string> args;

        // Loop through tokens to find special symbols
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i] == "<" && i + 1 < tokens.size()) {
                inFile = tokens[++i];
            } else if (tokens[i] == ">" && i + 1 < tokens.size()) {
                outFile = tokens[++i];
                append = false;
            } else if (tokens[i] == ">>" && i + 1 < tokens.size()) {
                outFile = tokens[++i];
                append = true;
            } else if (tokens[i] == "&") {
                isBackground = true;
            } else {
                args.push_back(tokens[i]); // Regular command arguments
            }
        }

        if (args.empty()) continue;

        // Save original Standard Input/Output file descriptors
        int saved_stdout = dup(STDOUT_FILENO);
        int saved_stdin  = dup(STDIN_FILENO);

        // Apply input redirection if requested
        if (!inFile.empty()) {
            int fd_in = open(inFile.c_str(), O_RDONLY);
            if (fd_in < 0) { perror("Input redirection failed"); continue; }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        // Apply output redirection if requested
        if (!outFile.empty()) {
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd_out = open(outFile.c_str(), flags, 0644);
            if (fd_out < 0) { perror("Output redirection failed"); continue; }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        // First, check if it's an internal built-in command
        if (!execute_builtin(args)) {
            // If not, treat it as an external command (program invocation)
            pid_t pid = fork();
            if (pid == 0) {
                // We are in the child process.
                // Convert C++ vector of strings to a C-style array of char pointers for execvp
                vector<char*> c_args;
                for (size_t i = 0; i < args.size(); ++i) {
                    c_args.push_back(const_cast<char*>(args[i].c_str()));
                }
                c_args.push_back(nullptr); // execvp requires a null pointer at the end

                execvp(c_args[0], c_args.data());
                perror("Command execution failed"); // Only prints if execvp fails
                exit(1);
            } else if (pid > 0) {
                // We are in the parent process. Wait unless it's a background process (&)
                if (!isBackground) {
                    waitpid(pid, NULL, 0);
                } else {
                    // Quick print so you know it launched in the background
                    dprintf(saved_stdout, "[Background process started: PID %d]\n", pid);
                }
            } else {
                perror("Fork failed");
            }
        }

        // Flush standard output buffer before restoring
        cout << flush;

        // Restore original Standard Input/Output
        dup2(saved_stdin, STDIN_FILENO);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdin);
        close(saved_stdout);
    }

    return 0;
}