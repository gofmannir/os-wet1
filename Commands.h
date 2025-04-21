// Ver: 10-4-2025
#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_

#include <vector>
#include <string>
#include <list>
#include <set>
#include <map>
#include <regex>
#include <unordered_map>

#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)

using namespace std;

class Command
{
    // TODO: Add your data members

public:
    const char *cmd_line;
    char *args[COMMAND_MAX_ARGS];
    int args_count;
    Command(const char *cmd_line) : cmd_line(cmd_line), args{}, args_count(0)
    {
        this->prepare();
    };

    virtual ~Command()
    {
        this->cleanup();
    }

    virtual void execute() = 0;

    virtual void prepare();
    virtual void cleanup();
    //  TODO: Add your extra methods if needed
};

class BuiltInCommand : public Command
{
public:
    BuiltInCommand(const char *cmd_line) : Command(cmd_line) {};

    virtual ~BuiltInCommand()
    {
    }
};

class ExternalCommand : public Command
{
public:
    string command;
    bool is_background_command = false;
    string original_cmd;
    ExternalCommand(const char *cmd_line, string &command, bool is_background_command, string &original_cmd);

    virtual ~ExternalCommand() {}

    void execute() override;
};

class RedirectionCommand : public Command
{
    // TODO: Add your data members
public:
    explicit RedirectionCommand(const char *cmd_line);

    virtual ~RedirectionCommand()
    {
    }

    void execute() override;
};

class PipeCommand : public Command
{
    // TODO: Add your data members
public:
    PipeCommand(const char *cmd_line);

    virtual ~PipeCommand()
    {
    }

    void execute() override;
};

class ChpromptCommand : public BuiltInCommand
{
public:
    ChpromptCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {};

    virtual ~ChpromptCommand() {}

    void execute() override;
};
class PwdCommand : public BuiltInCommand
{
public:
    PwdCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {};

    virtual ~PwdCommand() {}

    void execute() override;
};

class DiskUsageCommand : public Command
{
public:
    DiskUsageCommand(const char *cmd_line);

    virtual ~DiskUsageCommand()
    {
    }

    void execute() override;
};

class WhoAmICommand : public Command
{
public:
    WhoAmICommand(const char *cmd_line);

    virtual ~WhoAmICommand()
    {
    }

    void execute() override;
};

class NetInfo : public Command
{
    // TODO: Add your data members **BONUS: 10 Points**
public:
    NetInfo(const char *cmd_line);

    virtual ~NetInfo()
    {
    }

    void execute() override;
};

class ChangeDirCommand : public BuiltInCommand
{
    // TODO: Add your data members public:
public:
    char **plastPwd;
    ChangeDirCommand(const char *cmd_line, char **plastPwd) : BuiltInCommand(cmd_line), plastPwd(plastPwd) {}

    virtual ~ChangeDirCommand()
    {
        this->plastPwd = nullptr;
    }

    void execute() override;
};

class GetCurrDirCommand : public BuiltInCommand
{
public:
    GetCurrDirCommand(const char *cmd_line);

    virtual ~GetCurrDirCommand()
    {
    }

    void execute() override;
};

class ShowPidCommand : public BuiltInCommand
{
public:
    ShowPidCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {}

    virtual ~ShowPidCommand()
    {
    }

    void execute() override;
};

class JobsList;

class QuitCommand : public BuiltInCommand
{
    // TODO: Add your data members public:
public:
    JobsList *jobs;
    QuitCommand(const char *cmd_line, JobsList *jobs);

    virtual ~QuitCommand()
    {
        jobs = nullptr;
    }

    void execute() override;
};

class JobsList
{
public:
    class JobEntry
    {
        // TODO: Add your data members
    public:
        int job_id;
        bool stopped;
        pid_t pid;
        string command;
        JobEntry(int jobId, pid_t pid, const string &cmd, bool _stopped) : job_id(jobId), pid(pid), command(cmd),
                                                                           stopped(_stopped) {}
    };
    std::map<int, JobEntry> jobs;
    int next;

    // TODO: Add your data members
public:
    JobsList();

    ~JobsList() = default;

    void addJob(Command *cmd, pid_t pid, bool stopped = false);

    void printJobsList();

    void killAllJobs();

    void removeFinishedJobs();

    JobEntry *getJobById(int jobId);

    void removeJobById(int jobId);

    JobEntry *getLastJob(int *lastJobId);

    JobEntry *getLastStoppedJob(int *jobId);

    // TODO: Add extra methods or modify exisitng ones as needed
};

class JobsCommand : public BuiltInCommand
{
    // TODO: Add your data members
public:
    JobsList *jobs;
    JobsCommand(const char *cmd_line, JobsList *jobs);

    virtual ~JobsCommand()
    {
        jobs = nullptr;
    }

    void execute() override;
};

class KillCommand : public BuiltInCommand
{
    // TODO: Add your data members
public:
    JobsList *jobs;
    KillCommand(const char *cmd_line, JobsList *jobs);

    virtual ~KillCommand()
    {
        jobs = nullptr;
    }

    void execute() override;
};

class ForegroundCommand : public BuiltInCommand
{
    // TODO: Add your data members
public:
    JobsList *jobs;
    ForegroundCommand(const char *cmd_line, JobsList *jobs);

    virtual ~ForegroundCommand()
    {
        jobs = nullptr;
    }

    void execute() override;
};

class AliasCommand : public BuiltInCommand
{
public:
    AliasCommand(const char *cmd_line);

    virtual ~AliasCommand()
    {
    }

    void execute() override;
};

class UnAliasCommand : public BuiltInCommand
{
public:
    UnAliasCommand(const char *cmd_line);

    virtual ~UnAliasCommand()
    {
    }

    void execute() override;
};

class UnSetEnvCommand : public BuiltInCommand
{
public:
    UnSetEnvCommand(const char *cmd_line);

    virtual ~UnSetEnvCommand()
    {
    }

    void execute() override;
};

class WatchProcCommand : public BuiltInCommand
{
public:
    WatchProcCommand(const char *cmd_line);

    virtual ~WatchProcCommand()
    {
    }

    void execute() override;
};

class SmallShell
{
private:
    // TODO: Add your data members

    std::string prompt;
    char *plastPwd;

    SmallShell() : prompt("smash"), plastPwd(nullptr), fg_pid(-1)
    {
    }

public:
    pid_t fg_pid;
    JobsList jobs;
    set<string> reserved = {"chprompt", "quit", "showpid", "pwd", "cd", "jobs", "fg", "unalias", "alias", "kill", "listdir", "whoami", "netinfo"};

    unordered_map<string, string> aliases;
    std::list<std::pair<std::string, std::string>> alias_list;

    Command *CreateCommand(const char *cmd_line);

    SmallShell(SmallShell const &) = delete;     // disable copy ctor
    void operator=(SmallShell const &) = delete; // disable = operator
    static SmallShell &getInstance()             // make SmallShell singleton
    {
        static SmallShell instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    }

    std::string getPrompt()
    {
        return this->prompt;
    }

    std::string setPrompt(const std::string &val)
    {
        this->prompt = val;
    }

    char **getPlastPwd()
    {
        return &this->plastPwd;
    }

    void updatePlastPwd(char *newPtr)
    {
        if (this->plastPwd)
        {
            free(this->plastPwd);
        }
        this->plastPwd = newPtr;
    }

    ~SmallShell()
    {
        if (this->plastPwd)
        {
            free(this->plastPwd);
        }
    }

    void executeCommand(const char *cmd_line);

    list<std::pair<std::string, std::string>> &getAliases();
    unordered_map<string, string> &getAliasesMap();

    bool isReservedCommand(const string &command) const;
};

#endif // SMASH_COMMAND_H_
