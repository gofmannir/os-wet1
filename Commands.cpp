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

// TODO: Add your implementation for classes in Commands.h

/**
 * Creates and returns a pointer to Command class which matches the given command line (cmd_line)
 */
Command *SmallShell::CreateCommand(const char *cmd_line)
{

    string cmd_s = _trim(string(cmd_line));
    if (cmd_s.empty())
    {
        return nullptr; // Return nullptr for empty commands
    }
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));
    firstWord = _trim(firstWord);

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
    return nullptr;
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