#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <iomanip>
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

Command *SmallShell::CreateCommand(const char *cmd_line)
{

    string cmd_s = _trim(string(cmd_line));
    string org_cmd_line = cmd_s;

    if (cmd_s.empty())
    {
        return nullptr; // Return nullptr for empty commands
    }
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));
    firstWord = _trim(firstWord);

    bool is_background_command = _isBackgroundComamnd(cmd_line);
    if (is_background_command)
    {
        _removeBackgroundSign(const_cast<char *>(cmd_line));
        _removeBackgroundSign(const_cast<char *>(cmd_s.c_str()));
    }

    if (firstWord == "jobs")
    {
        return new JobsCommand(cmd_line, &jobs);
    }

    if (firstWord == "fg")
    {
        return new ForegroundCommand(cmd_line, &jobs);
    }
    if (firstWord == "quit")
    {
        return new QuitCommand(cmd_line, &jobs);
    }
    if (firstWord == "kill")
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
    if (firstWord.compare("pwd") == 0)
    {
        return new PwdCommand(cmd_line);
    }
    if (firstWord.compare("cd") == 0)
    {
        return new ChangeDirCommand(cmd_line, getPlastPwd());
    }
    // For example:
    /*
    string cmd_s = _trim(string(cmd_line));
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));

    if (firstWord.compare("pwd") == 0) {
      return new GetCurrDirCommand(cmd_line);
    }
    else if (firstWord.compare("showpid") == 0) {
      return new ShowPidCommand(cmd_line);
    }
    else if ...
    .....
    else {
      return new ExternalCommand(cmd_line);
    }
    */
    // ExternalCommand::ExternalCommand(const char *cmd_line, string &com, bool is_background_command, string &original) : Command(cmd_line), command(com), is_background_command(is_background_command), original_cmd(original) {}

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

void ShowPidCommand::execute()
{
    pid_t pid = getpid();
    if (pid == -1)
    {
        cerr << "smash error: getpid failed" << endl;
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
        cerr << "smash error: getcwd failed" << endl;
    }
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
        cerr << "smash error: getcwd failed" << endl;
        return;
    }

    if (chdir(path.c_str()) == -1)
    {
        cerr << "smash error: chdir failed" << endl;
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