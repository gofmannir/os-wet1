#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <iomanip>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include "Commands.h"

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY() \
    cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT() \
    cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

string _ltrim(const std::string &s)
{
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string &s)
{
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string &s)
{
    return _rtrim(_ltrim(s));
}

int _parseCommandLine(const char *cmd_line, char **args)
{
    FUNC_ENTRY()
    int i = 0;
    std::istringstream iss(_trim(string(cmd_line)).c_str());
    for (std::string s; iss >> s;)
    {
        args[i] = (char *)malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;

    FUNC_EXIT()
}

bool _isBackgroundComamnd(const char *cmd_line)
{
    const string str(cmd_line);
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char *cmd_line)
{
    const string str(cmd_line);
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos)
    {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&')
    {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

void removeQuotes(char *args[], int arg_count)
{
    for (int i = 1; i < arg_count; ++i)
    {
        std::string arg(args[i]);
        if (arg.size() > 1 && ((arg.front() == '"' && arg.back() == '"') || (arg.front() == '\'' && arg.back() == '\'')))
        {
            arg = arg.substr(1, arg.size() - 2);
            strcpy(args[i], arg.c_str());
        }
    }
}

ExternalCommand::ExternalCommand(const char *cmd_line, string &com, bool is_background_command, string &original) : Command(cmd_line), command(com), is_background_command(is_background_command), original_cmd(original) {}
void ExternalCommand::execute()
{
    cmd_line = command.c_str();
    removeQuotes(this->args, this->args_count);

    pid_t pid = fork();
    if (pid == -1)
    {
        cerr << "smash error: fork failed" << endl;
    }

    if (pid == 0)
    {
        // Child process
        setpgrp();
        if (strchr(cmd_line, '*') || strchr(cmd_line, '?'))
        {
            // Complex command
            char *bash_args[] = {(char *)"/bin/bash", (char *)"-c", (char *)cmd_line, nullptr};
            execv("/bin/bash", bash_args);
        }
        else
        {
            // Simple command
            execvp(args[0], args);
        }
        cerr << "smash error: exec failed" << endl;
        exit(1);
    }
    else
    {
        // Parent process
        if (!is_background_command)
        {
            // We should wait for this command to finish. no & at the end.
            SmallShell &smash = SmallShell::getInstance();
            smash.fg_pid = pid;
            waitpid(pid, nullptr, 0);
            smash.fg_pid = -1;
        }
        else
        {
            // in this case, it is added to the jobs list
            SmallShell::getInstance().jobs.addJob(this, pid, false);
        }
    }
}

JobsCommand::JobsCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line), jobs(jobs) {}
ForegroundCommand::ForegroundCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line), jobs(jobs) {}
QuitCommand::QuitCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line), jobs(jobs) {}
KillCommand::KillCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line), jobs(jobs) {}

void QuitCommand::execute()
{
    if (this->args_count > 1 && strcmp(args[1], "kill") == 0)
    {
        this->jobs->killAllJobs();
    }

    exit(0);
}

void KillCommand::execute()
{
    jobs->removeFinishedJobs();
    SmallShell &smash = SmallShell::getInstance();
    auto smash_self_pid = smash.fg_pid;

    if (this->args_count != 3 || this->args[1][0] != '-')
    {
        std::cerr << "smash error: kill: invalid arguments" << std::endl;
        return;
    }

    int signum;
    try
    {
        signum = std::stoi(this->args[1] + 1); // Skip the '-' character
    }
    catch (const std::invalid_argument &e)
    {
        std::cerr << "smash error: kill: invalid arguments" << std::endl;
        return;
    }

    string job_id(this->args[2]);
    if (!std::all_of(job_id.begin(), job_id.end(), ::isdigit))
    {
        std::cerr << "smash error: kill: invalid arguments" << std::endl;
        return;
    }
    int id = std::stoi(job_id);

    JobsList::JobEntry *job = jobs->getJobById(id);
    if (!job)
    {
        std::cerr << "smash error: kill: job-id " << job_id << " does not exist" << std::endl;
        return;
    }

    std::cout << "signal number " << signum << " was sent to pid " << job->pid << std::endl;
    if (kill(job->pid, signum) == -1)
    {
        cerr << "smash error: kill failed" << endl;
        return;
    }

    if (job->pid == smash_self_pid)
    {
        smash.fg_pid = -1;
    }
}

AliasCommand::AliasCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {}
UnAliasCommand::UnAliasCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {}
void AliasCommand::execute()
{
    SmallShell &smash = SmallShell::getInstance();
    auto &aliases = smash.getAliasesMap();
    auto &alias_list = smash.getAliases();

    // Regex to validate the alias format
    std::regex alias_regex("^alias ([a-zA-Z0-9_]+)='([^']*)'$");
    std::smatch match;

    if (this->args_count == 1)
    {
        // list all
        for (const auto &entry : alias_list)
        {
            std::cout << entry.first << "='" << entry.second << "'" << std::endl;
        }
        return;
    }

    // make a new alias
    string input = _trim(string(cmd_line));
    if (std::regex_match(input, match, alias_regex))
    {
        string name = match[1];    // alias name
        string command = match[2]; // command

        // Check if name is a reserved keyword or existing alias
        if (aliases.find(name) != aliases.end() || smash.isReservedCommand(name))
        {
            std::cerr << "smash error: alias: " << name << " already exists or is a reserved command" << std::endl;
            return;
        }

        // save
        aliases[name] = command;
        alias_list.emplace_back(name, command);
    }
    else
    {
        // Invalid syntax
        std::cerr << "smash error: alias: invalid alias format" << std::endl;
    }
}
// unalias command (built in command)
void UnAliasCommand::execute()
{
    SmallShell &smash = SmallShell::getInstance();
    auto &aliases = smash.getAliasesMap();
    auto &aliases_list = smash.getAliases();

    // no arguments provided
    if (args_count == 1)
    {
        std::cerr << "smash error: unalias: not enough arguments" << std::endl;
        return;
    }

    // Iterate through the provided alias names
    for (int i = 1; i < args_count; i++)
    {
        string alias_name(args[i]);

        // Check if the alias exists
        if (aliases.find(alias_name) == aliases.end())
        {
            std::cerr << "smash error: unalias: " << alias_name << " alias does not exist" << std::endl;
            return;
        }

        // Remove
        aliases.erase(alias_name);
        aliases_list.remove_if([&alias_name](const std::pair<std::string, std::string> &alias)
                               { return alias.first == alias_name; });
    }
}

RedirectionCommand::RedirectionCommand(const std::string &cmd_line, const std::string &command, const std::string &output_file, bool append)
    : Command(cmd_line.c_str()), output_file(output_file), command(command), append(append) {}

void RedirectionCommand::execute()
{
    string run_command = string(this->command);
    string out_file = string(this->output_file);
    int temp_stdout_fd = dup(STDOUT_FILENO);
    if (temp_stdout_fd == -1)
    {
        perror("smash error: dup failed");
        return;
    }

    int open_flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int fd = open(out_file.c_str(), open_flags, 0644);
    if (fd == -1)
    {
        perror("smash error: open failed");
        return;
    }

    if (dup2(fd, STDOUT_FILENO) == -1)
    {
        perror("smash error: dup2 failed");
        close(fd);
        return;
    }

    close(fd);

    // Execute the run_command
    SmallShell &smash = SmallShell::getInstance();
    smash.executeCommand(run_command.c_str());

    // Restore stdout
    if (dup2(temp_stdout_fd, STDOUT_FILENO) == -1)
    {
        perror("smash error: dup2 failed");
    }
    close(temp_stdout_fd);
}

// Reads a line from the given file descriptor into the buffer, up to max_len characters.
// Returns the number of characters read, or -1 on error.
static ssize_t readLine(int fileDescriptor, char *outputBuffer, size_t maxLength)
{
    size_t currentIndex = 0; // Tracks the current position in the buffer
    char currentChar;        // Temporary storage for the character being read

    // Loop until the buffer is full or a newline/EOF is encountered
    while (currentIndex < maxLength - 1)
    {
        ssize_t bytesRead = read(fileDescriptor, &currentChar, 1); // Read one character at a time
        if (bytesRead < 0)
        {
            // Error occurred during read
            perror("smash error: read failed");
            return -1;
        }
        if (bytesRead == 0)
        {
            // End of file (EOF) reached
            break;
        }
        if (currentChar == '\n')
        {
            // Newline character indicates the end of the line
            outputBuffer[currentIndex++] = currentChar;
            break;
        }
        // Store the character in the buffer
        outputBuffer[currentIndex++] = currentChar;
    }

    // Null-terminate the string
    outputBuffer[currentIndex] = '\0';
    return (ssize_t)currentIndex; // Return the number of characters read
}

Command *SmallShell::CreateCommand(const char *cmd_line)
{
    // Check if the command matches an alias
    SmallShell &smash = SmallShell::getInstance();

    string cmd_s = _trim(string(cmd_line));
    string org_cmd_line = cmd_s;

    if (cmd_s.empty())
    {
        return nullptr; // Return nullptr for empty commands
    }

    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));
    firstWord = _trim(firstWord);

    auto &aliases = smash.getAliasesMap();
    auto aliasIter = aliases.find(firstWord);
    if (aliasIter != aliases.end())
    {
        // Expand the alias
        cmd_s = _trim(aliasIter->second + cmd_s.substr(firstWord.size()));
        firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));
        cmd_line = strdup(cmd_s.c_str());
    }

    // Pipe logic
    size_t pipe_position = cmd_s.find("|&");
    bool is_stderr_redirect = (pipe_position != string::npos);

    if (!is_stderr_redirect)
    {
        pipe_position = cmd_s.find('|');
    }

    if (pipe_position != string::npos)
    {
        // Split the command into two parts
        std::string first_command = _trim(cmd_s.substr(0, pipe_position));
        std::string second_command = _trim(cmd_s.substr(pipe_position + (is_stderr_redirect ? 2 : 1)));

        // Return a PipeCommand object
        return new PipeCommand(cmd_line, first_command, second_command, is_stderr_redirect);
    }

    bool is_background_command = _isBackgroundComamnd(cmd_line);
    if (is_background_command)
    {
        _removeBackgroundSign(const_cast<char *>(cmd_line));
        _removeBackgroundSign(const_cast<char *>(cmd_s.c_str()));
    }

    // Check for redirection
    size_t redir_pos = cmd_s.find('>');
    if (redir_pos != string::npos)
    {
        bool append = (cmd_s[redir_pos + 1] == '>');
        auto command = _trim(cmd_s.substr(0, redir_pos));
        string output_file = _trim(cmd_s.substr(redir_pos + (append ? 2 : 1)));
        return new RedirectionCommand(cmd_line, command, output_file, append);
    }

    if (firstWord.compare("alias") == 0)
    {
        return new AliasCommand(cmd_line);
    }

    if (firstWord.compare("jobs") == 0)
    {
        return new JobsCommand(cmd_line, &jobs);
    }

    if (firstWord.compare("fg") == 0)
    {
        return new ForegroundCommand(cmd_line, &jobs);
    }
    if (firstWord.compare("quit") == 0)
    {
        return new QuitCommand(cmd_line, &jobs);
    }
    if (firstWord.compare("kill") == 0)
    {
        return new KillCommand(cmd_line, &jobs);
    }
    if (firstWord.compare("chprompt") == 0)
    {
        return new ChpromptCommand(cmd_line);
    }
    if (firstWord.compare("showpid") == 0)
    {
        return new ShowPidCommand(cmd_line);
    }
    if (firstWord.compare("unalias") == 0)
    {
        return new UnAliasCommand(cmd_line);
    }
    if (firstWord.compare("unsetenv") == 0)
    {
        return new UnSetEnvCommand(cmd_line);
    }
    if (firstWord.compare("pwd") == 0)
    {
        return new PwdCommand(cmd_line);
    }
    if (firstWord.compare("cd") == 0)
    {
        return new ChangeDirCommand(cmd_line, getPlastPwd());
    }
    if (firstWord.compare("watchproc") == 0)
    {
        return new WatchProcCommand(cmd_line);
    }
    if (firstWord.compare("whoami") == 0)
    {
        return new WhoAmICommand(cmd_line);
    }

    if (firstWord.compare("netinfo") == 0)
    {
        return new NetInfo(cmd_line);
    }

    string updated_str_after_aliases(cmd_line);
    return new ExternalCommand(cmd_line, updated_str_after_aliases, is_background_command, org_cmd_line);
}

void SmallShell::executeCommand(const char *cmd_line)
{
    // TODO: Add your implementation here
    // for example:
    Command *cmd = CreateCommand(cmd_line);
    if (cmd)
    {
        cmd->execute();
        delete cmd;
    }

    // Please note that you must fork smash process for some commands (e.g., external commands....)
}

void Command::prepare()
{
    this->args_count = _parseCommandLine(cmd_line, this->args);
}
void Command::cleanup()
{
    for (int i = 0; i < this->args_count; ++i)
    {
        free(this->args[i]);
    }
}

void ChpromptCommand::execute()
{
    SmallShell &smash = SmallShell::getInstance();
    if (this->args_count == 1)
    {
        smash.setPrompt("smash");
    }
    else
    {
        smash.setPrompt(this->args[1]);
    }
}

bool UnSetEnvCommand::getEnv(const char *env_name)
{
    std::string env_name_str(env_name);
    std::string env_value;

    // Open the /proc/<pid>/environ file
    std::string environ_path = "/proc/" + std::to_string(getpid()) + "/environ";
    int environ_fd = open(environ_path.c_str(), O_RDONLY);
    if (environ_fd == -1)
    {
        return false;
    }

    char buffer[BUF_SIZE];
    ssize_t bytes_read = 0;
    std::string leftover_data;

    while ((bytes_read = readLine(environ_fd, buffer, sizeof(buffer))) > 0)
    {
        std::string data(buffer, bytes_read);
        leftover_data += data;

        size_t pos = 0;
        while ((pos = leftover_data.find('\0')) != std::string::npos)
        {
            std::string env_entry = leftover_data.substr(0, pos);
            leftover_data = leftover_data.substr(pos + 1);

            size_t equal_pos = env_entry.find('=');
            if (equal_pos != std::string::npos)
            {
                std::string key = env_entry.substr(0, equal_pos);
                std::string value = env_entry.substr(equal_pos + 1);

                if (key == env_name_str)
                {
                    env_value = value;
                    return true;
                }
            }
        }
    }

    return false;
}

void UnSetEnvCommand::removeEnv(const char *env_name)
{
    extern char **environ;
    // Remove the environment variable from the environ array
    char **env = environ;
    while (*env)
    {
        if (strncmp(*env, env_name, strlen(env_name)) == 0 && (*env)[strlen(env_name)] == '=')
        {
            char **next = env + 1;
            while (*next)
            {
                *env = *next;
                env++;
                next++;
            }
            *env = nullptr;
            break;
        }
        env++;
    }
}

void UnSetEnvCommand::execute()
{
    if (this->args_count == 1)
    {
        cerr << "smash error: unsetenv: not enough arguments" << endl;
    }

    SmallShell &smash = SmallShell::getInstance();
    for (int i = 1; i < this->args_count; ++i)
    {
        if (!this->getEnv(this->args[i]))
        {
            // env doesnt exists
            cerr << "smash error: unsetenv: " << this->args[i] << " does not exist" << endl;
            return;
        }

        this->removeEnv(this->args[i]);
    }
}

void ShowPidCommand::execute()
{
    pid_t pid = getpid();
    if (pid == -1)
    {
        perror("smash error: getpid failed");
    }
    else
    {
        std::cout << "smash pid is " << pid << std::endl;
    }
}

void PwdCommand::execute()
{
    char cwd_buf[COMMAND_MAX_LENGTH];
    auto pwd = getcwd(cwd_buf, sizeof(cwd_buf));
    if (pwd != nullptr)
    {
        std::cout << pwd << std::endl;
    }
    else
    {
        perror("smash error: getcwd failed");
    }
}

void WatchProcCommand::execute()
{
    if (this->args_count != 2)
    {
        cerr << "smash error: watchproc: invalid arguments" << endl;
        return;
    }

    pid_t pid = std::stoi(this->args[1]);
    if (kill(pid, 0) == -1)
    {
        cerr << "smash error: watchproc: pid " << pid << " does not exist" << endl;
        return;
    }

    // Open the /proc/<pid>/stat file to get CPU and memory usage
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    int stat_fd = open(stat_path.c_str(), O_RDONLY);
    if (stat_fd == -1)
    {
        // TODO : maybe other msg?
        cerr << "smash error: watchproc: pid " << pid << " does not exist" << endl;
        return;
    }

    char stat_buf[BUF_SIZE];
    ssize_t stat_read = readLine(stat_fd, stat_buf, sizeof(stat_buf));
    if (stat_read <= 0)
    {
        // TODO : maybe another msg?
        cerr << "smash error: watchproc: pid " << pid << " does not exist" << endl;
        close(stat_fd);
        return;
    }
    close(stat_fd);

    // Parse the /proc/<pid>/stat file
    std::istringstream stat_stream(stat_buf);
    std::string token;
    std::vector<std::string> stat_fields;
    while (stat_stream >> token)
    {
        stat_fields.push_back(token);
    }

    // Ensure we have enough fields
    if (stat_fields.size() < 24)
    {
        // TODO : make sure need this?
        cerr << "smash error: watchproc: pid " << pid << " does not exist" << endl;
        return;
    }

    // Extract CPU usage (utime + stime)
    long utime = std::stol(stat_fields[13]);
    long stime = std::stol(stat_fields[14]);
    long total_time = utime + stime;

    // Get system uptime from /proc/uptime
    int uptime_fd = open("/proc/uptime", O_RDONLY);
    if (uptime_fd == -1)
    {
        // TODO : make sure err msg
        cerr << "smash error: watchproc: pid " << pid << " does not exist" << endl;
        return;
    }

    char uptime_buf[BUF_SIZE];
    ssize_t uptime_read = readLine(uptime_fd, uptime_buf, sizeof(uptime_buf));
    if (uptime_read <= 0)
    {
        // TODO : Make sure err msg
        cerr << "smash error: watchproc: pid " << pid << " does not exist" << endl;
        close(uptime_fd);
        return;
    }
    close(uptime_fd);

    double uptime;
    std::istringstream uptime_stream(uptime_buf);
    uptime_stream >> uptime;

    // Calculate CPU usage
    long hertz = sysconf(_SC_CLK_TCK);
    double seconds = uptime - (std::stol(stat_fields[21]) / hertz);
    double cpu_usage = 100.0 * ((total_time / static_cast<double>(hertz)) / seconds);

    // Get memory usage from /proc/<pid>/statm
    std::string statm_path = "/proc/" + std::to_string(pid) + "/statm";
    int statm_fd = open(statm_path.c_str(), O_RDONLY);
    if (statm_fd == -1)
    {
        cerr << "smash error: watchproc: pid " << pid << " does not exist" << endl;
        return;
    }

    char statm_buf[BUF_SIZE];
    ssize_t statm_read = readLine(statm_fd, statm_buf, sizeof(statm_buf));
    if (statm_read <= 0)
    {
        cerr << "smash error: watchproc: pid " << pid << " does not exist" << endl;
        close(statm_fd);
        return;
    }
    close(statm_fd);

    // Parse memory usage
    std::istringstream statm_stream(statm_buf);
    long resident_pages;
    statm_stream >> token >> resident_pages;
    long page_size = sysconf(_SC_PAGESIZE);
    double memory_usage_mb = (resident_pages * page_size) / (1024.0 * 1024.0);

    // Print CPU and memory usage
    std::cout << "PID: " << pid << " | CPU Usage: " << std::fixed << std::setprecision(1) << cpu_usage
              << "% | Memory Usage: " << std::fixed << std::setprecision(1) << memory_usage_mb << " MB" << std::endl;
}

void ChangeDirCommand::execute()
{
    if (this->args_count == 1)
    {
        return;
    }

    if (this->args_count > 2)
    {
        cerr << "smash error: cd: too many arguments" << endl;
        return;
    }

    std::string path(this->args[1]);
    if (path == "-" && *plastPwd == nullptr)
    {
        cerr << "smash error: cd: OLDPWD not set" << endl;
        return;
    }

    if (path == "-")
    {
        path = *this->plastPwd;
    }

    char current_directory[COMMAND_MAX_LENGTH];
    if (!getcwd(current_directory, sizeof(current_directory)))
    {
        perror("smash error: getcwd failed");
        return;
    }

    if (chdir(path.c_str()) == -1)
    {
        perror("smash error: chdir failed");
        return;
    }

    SmallShell &smash = SmallShell::getInstance();
    smash.updatePlastPwd(strdup(current_directory));
}

// fg command (built in command)
void ForegroundCommand::execute()
{
    jobs->removeFinishedJobs();

    if (this->args_count > 2)
    {
        std::cerr << "smash error: fg: invalid arguments" << std::endl;
        return;
    }

    JobsList::JobEntry *job = nullptr;
    int job_id = 0;

    if (this->args_count == 1)
    {
        // in case no job id is given, we should take the last job
        job = jobs->getLastJob(&job_id);
        if (!job)
        {
            std::cerr << "smash error: fg: jobs list is empty" << std::endl;
            return;
        }
    }
    else
    {
        std::string jobIdStr(args[1]);
        if (!std::all_of(jobIdStr.begin(), jobIdStr.end(), ::isdigit))
        {
            std::cerr << "smash error: fg: invalid arguments" << std::endl;
            return;
        }
        job_id = std::stoi(jobIdStr);
        job = jobs->getJobById(job_id);
        if (!job)
        {
            std::cerr << "smash error: fg: job-id " << job_id << " does not exist" << std::endl;
            return;
        }
    }

    if (job->stopped)
    {
        if (kill(job->pid, SIGCONT) == -1)
        {
            perror("smash error: SIGCONT failed");
            return;
        }
        job->stopped = false;
    }

    SmallShell &smash = SmallShell::getInstance();
    smash.fg_pid = job->pid;

    std::cout << job->command << " " << job->pid << std::endl;
    int status;
    if (waitpid(job->pid, &status, WUNTRACED) == -1)
    {
        perror("smash error: waitpid failed");
    }

    if (WIFEXITED(status) || WIFSIGNALED(status))
    {
        jobs->removeJobById(job_id);
    }
    else if (WIFSTOPPED(status))
    {
        job->stopped = true;
    }

    smash.fg_pid = -1;
}

void JobsCommand::execute()
{
    this->jobs->printJobsList();
}

JobsList::JobsList() : next(1) {}

void JobsList::addJob(Command *cmd, pid_t pid, bool stopped)
{
    removeFinishedJobs();
    int job_id = next++;
    jobs.emplace(job_id, JobEntry(job_id, pid, cmd->cmd_line, stopped));
}

void JobsList::printJobsList()
{
    removeFinishedJobs(); // Ensure finished jobs are not printed
    for (const auto &pair : jobs)
    {
        const JobEntry &job = pair.second;
        std::cout << "[" << job.job_id << "] " << job.command << std::endl;
    }
}

void JobsList::killAllJobs()
{
    removeFinishedJobs();
    cout << "smash: sending SIGKILL signal to " << jobs.size() << " jobs:" << endl;
    for (const auto &pair : jobs)
    {
        const JobEntry &job = pair.second;
        cout << job.pid << ": " << job.command << endl;
        int _res = kill(job.pid, SIGKILL);

        if (_res == -1)
        {
            perror("smash error: kill failed");
            return;
        }
    }
    jobs.clear();
}

void JobsList::removeFinishedJobs()
{
    int status;
    pid_t curr_job_id;
    SmallShell &smash = SmallShell::getInstance();
    for (auto it = jobs.begin(); it != jobs.end();)
    {
        curr_job_id = waitpid(it->second.pid, &status, WNOHANG);
        if (curr_job_id == -1 || (!it->second.stopped && curr_job_id > 0))
        {
            if (it->second.pid == smash.fg_pid)
            {
                smash.fg_pid = -1;
            }
            it = jobs.erase(it);
        }
        else
        {
            ++it;
        }
    }
    if (!jobs.empty())
    {
        next = (--jobs.end())->first + 1;
    }
    else
    {
        next = 1;
    }
}

JobsList::JobEntry *JobsList::getJobById(int jobId)
{
    removeFinishedJobs();
    auto it = jobs.find(jobId);
    if (it != jobs.end())
    {
        return &(it->second);
    }
    return nullptr;
}

void JobsList::removeJobById(int jobId)
{
    jobs.erase(jobId);
    if (!jobs.empty())
    {
        next = (--jobs.end())->first + 1;
    }
    else
    {
        next = 1;
    }
}

JobsList::JobEntry *JobsList::getLastJob(int *lastJobId)
{
    removeFinishedJobs();
    if (jobs.empty())
    {
        return nullptr;
    }
    auto it = --jobs.end();
    if (lastJobId)
    {
        *lastJobId = it->first;
    }
    return &(it->second);
}

JobsList::JobEntry *JobsList::getLastStoppedJob(int *jobId)
{
    removeFinishedJobs();
    int max = -1;
    for (const auto &job : jobs)
    {
        if (job.second.stopped && job.first > max)
        {
            max = job.first;
        }
    }
    if (max == -1)
    {
        return nullptr;
    }
    if (jobId)
    {
        *jobId = max;
    }
    return &(jobs.find(max)->second);
}

std::list<std::pair<std::string, std::string>> &SmallShell::getAliases()
{
    return this->alias_list;
}

unordered_map<string, string> &SmallShell::getAliasesMap()
{
    return this->aliases;
}

bool SmallShell::isReservedCommand(const string &command) const
{
    return this->reserved.find(command) != this->reserved.end();
}

PipeCommand::PipeCommand(const std::string &cmd_line, const std::string &part1, const std::string &part2, bool is_stderr) : Command(cmd_line.c_str()), cmd1(part1), cmd2(part2), is_stderr(is_stderr) {}

void PipeCommand::execute()
{

    int pipe_fd[2];

    // creating a pipe object with syscall
    if (pipe(pipe_fd) == -1)
    {
        perror("smash error: pipe failed");
        return;
    }

    SmallShell &smash = SmallShell::getInstance();
    pid_t pid1 = fork();

    if (pid1 == -1)
    {
        perror("smash error: fork failed");
        return;
    }

    if (pid1 == 0)
    {
        // Child
        setpgrp();
        if (close(pipe_fd[0]) == -1)
        {
            perror("smash error: close failed");
            exit(1);
        }

        // 1 is for write end in the pipe
        // redirects out/err to the pipe
        // dup2 for closing the fd to make sure other reads will end
        if (dup2(pipe_fd[1], is_stderr ? STDERR_FILENO : STDOUT_FILENO) == -1)
        {
            perror("smash error: dup2 failed");
            exit(1);
        }

        if (close(pipe_fd[1]) == -1)
        {
            perror("smash error: close failed");
            exit(1);
        }

        // Execute
        Command *cmd1 = smash.CreateCommand(this->cmd1.c_str());
        if (cmd1)
        {
            cmd1->execute();
            delete cmd1;
            exit(0);
        }
        exit(1);
    }

    pid_t pid2 = fork();
    if (pid2 == -1)
    {
        perror("smash error: fork failed");
        return;
    }

    if (pid2 == 0)
    {
        // Child
        setpgrp();

        // this process child should not have the write fd to the pipe so we close it
        if (close(pipe_fd[1]) == -1)
        {
            perror("smash error: close failed");
            exit(1);
        }

        // Again like before
        if (dup2(pipe_fd[0], STDIN_FILENO) == -1)
        {
            perror("smash error: dup2 failed");
            exit(1);
        }

        if (close(pipe_fd[0]) == -1)
        {
            perror("smash error: close failed");
            exit(1);
        }

        // Execute
        Command *cmd2 = smash.CreateCommand(this->cmd2.c_str());
        if (cmd2)
        {
            cmd2->execute();
            delete cmd2;
            exit(0);
        }
        exit(1);
    }

    // closing in the parent
    if (close(pipe_fd[0]) == -1)
    {
        perror("smash error: close failed");
    }

    if (close(pipe_fd[1]) == -1)
    {
        perror("smash error: close failed");
    }

    // wait both the childs to finish and see the out of the cmd2 child
    waitpid(pid1, nullptr, 0);
    waitpid(pid2, nullptr, 0);
}

WhoAmICommand::WhoAmICommand(const char *cmd_line) : Command(cmd_line) {}
void WhoAmICommand::execute()
{
    uid_t effectiveUserId = geteuid(); // Get the effective user ID
    std::string username, homeDirectory;

    // Retrieve username and home directory
    this->fetchUserInfo(effectiveUserId, username, homeDirectory);

    // Print the result
    std::cout << username << " " << homeDirectory << std::endl;
}

void WhoAmICommand::fetchUserInfo(uid_t userId, std::string &username, std::string &homeDirectory)
{
    int passwdFileDescriptor = open("/etc/passwd", O_RDONLY);
    if (passwdFileDescriptor == -1)
    {
        perror("smash error: open failed");
        return;
    }

    char buffer[4096];
    ssize_t bytesRead = 0;
    std::string leftoverData;

    while ((bytesRead = read(passwdFileDescriptor, buffer, sizeof(buffer))) > 0)
    {
        string data(buffer, bytesRead);
        leftoverData += data;

        size_t lineEnd = 0;
        while ((lineEnd = leftoverData.find('\n')) != std::string::npos)
        {
            std::string line = leftoverData.substr(0, lineEnd);
            leftoverData = leftoverData.substr(lineEnd + 1);

            std::string user, uidStr, home;
            size_t field = 0, start = 0;
            uid_t entryUid = -1;

            for (size_t i = 0; i <= line.size(); ++i)
            {
                if (i == line.size() || line[i] == ':')
                {
                    std::string fieldValue = line.substr(start, i - start);
                    start = i + 1;

                    switch (field++)
                    {
                    case 0:
                        user = fieldValue; // Username
                        break;
                    case 2:
                        uidStr = fieldValue; // UID
                        entryUid = std::stoi(uidStr);
                        break;
                    case 5:
                        home = fieldValue; // Home directory
                        break;
                    }
                }
            }

            if (entryUid == userId)
            {
                username = user;
                homeDirectory = home;
                if (close(passwdFileDescriptor) == -1)
                {
                    perror("smash error: close failed");
                }
                passwdFileDescriptor = -1;
                return;
            }
        }
    }

    if (bytesRead == -1)
    {
        perror("smash error: read failed");
    }

    if (passwdFileDescriptor != -1)
    {
        if (close(passwdFileDescriptor) == -1)
        {
            perror("smash error: close failed");
        }
        passwdFileDescriptor = -1;
    }
}

NetInfo::NetInfo(const char *cmd_line) : Command(cmd_line) {}

void NetInfo::execute()
{
    if (args_count < 2)
    {
        std::cerr << "smash error: netinfo: interface not specified" << std::endl;
        return;
    }

    std::string ifname = args[1];

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("smash error: socket failed");
        return;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);

    // Get IP Address
    if (ioctl(sock, SIOCGIFADDR, &ifr) < 0)
    {
        std::cerr << "smash error: netinfo: interface " << ifname << " does not exist" << std::endl;
        close(sock);
        return;
    }
    struct sockaddr_in *ip_addr = (struct sockaddr_in *)&ifr.ifr_addr;
    char ip_str[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &ip_addr->sin_addr, ip_str, sizeof(ip_str)))
    {
        perror("smash error: inet_ntop failed");
        close(sock);
        return;
    }

    // Get Netmask
    if (ioctl(sock, SIOCGIFNETMASK, &ifr) < 0)
    {
        perror("smash error: ioctl failed");
        close(sock);
        return;
    }
    struct sockaddr_in *nm_addr = (struct sockaddr_in *)&ifr.ifr_netmask;
    char mask_str[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &nm_addr->sin_addr, mask_str, sizeof(mask_str)))
    {
        perror("smash error: inet_ntop failed");
        close(sock);
        return;
    }

    close(sock);

    // Get Default Gateway from /proc/net/route
    std::string gateway_str;
    {
        int fd = open("/proc/net/route", O_RDONLY);
        if (fd < 0)
        {
            perror("smash error: open failed");
            // Not having route info isn't fatal to the rest; we just won't print gateway.
        }
        else
        {
            char line[BUF_SIZE];
            // Skip the header line
            if (readLine(fd, line, sizeof(line)) < 0)
            {
                // readLine already printed perror, just close and continue
                close(fd);
            }
            else
            {
                while (true)
                {
                    ssize_t rd = readLine(fd, line, sizeof(line));
                    if (rd < 0)
                    {
                        // readLine printed error
                        break;
                    }
                    if (rd == 0)
                    {
                        // EOF
                        break;
                    }

                    // Parse the line
                    char iface[IFNAMSIZ];
                    char dest[32], gate[32];
                    if (sscanf(line, "%s %s %s", iface, dest, gate) == 3)
                    {
                        if (ifname == iface && strcmp(dest, "00000000") == 0)
                        {
                            unsigned long g;
                            if (sscanf(gate, "%lx", &g) == 1)
                            {
                                struct in_addr ga;
                                ga.s_addr = g;
                                char g_ip[INET_ADDRSTRLEN];
                                if (!inet_ntop(AF_INET, &ga, g_ip, sizeof(g_ip)))
                                {
                                    perror("smash error: inet_ntop failed");
                                    break;
                                }
                                gateway_str = g_ip;
                                break;
                            }
                        }
                    }
                }
                close(fd);
            }
        }
    }

    // Get DNS servers from /etc/resolv.conf
    std::string dns_list;
    {
        int fd = open("/etc/resolv.conf", O_RDONLY);
        if (fd < 0)
        {
            // If resolv.conf can't be opened, no DNS info
            perror("smash error: open failed");
        }
        else
        {
            char line[BUF_SIZE];
            while (true)
            {
                ssize_t n = readLine(fd, line, sizeof(line));
                if (n < 0)
                {
                    // readLine printed error
                    break;
                }
                if (n == 0)
                {
                    // EOF
                    break;
                }
                char *p = line;
                while (*p == ' ' || *p == '\t')
                    p++;
                if (strncmp(p, "nameserver", 10) == 0)
                {
                    p += 10;
                    while (*p == ' ' || *p == '\t')
                        p++;
                    if (*p != '\0' && *p != '\n')
                    {
                        std::string dns_ip(p);
                        // Remove trailing newline
                        dns_ip.erase(dns_ip.find_last_not_of("\r\n") + 1);
                        if (!dns_list.empty())
                            dns_list += ", ";
                        dns_list += dns_ip;
                    }
                }
            }
            close(fd);
        }
    }

    // Print results
    std::cout << "IP Address: " << ip_str << std::endl;
    std::cout << "Subnet Mask: " << mask_str << std::endl;
    if (!gateway_str.empty())
    {
        std::cout << "Default Gateway: " << gateway_str << std::endl;
    }
    if (!dns_list.empty())
    {
        std::cout << "DNS Servers: " << dns_list << std::endl;
    }
}