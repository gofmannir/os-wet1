#include <iostream>
#include <signal.h>
#include "signals.h"
#include "Commands.h"

using namespace std;

void ctrlCHandler(int sig_num)
{
    cout << "smash: got ctrl-C" << endl;
    SmallShell &smash = SmallShell::getInstance();
    smash.jobs.removeFinishedJobs();
    pid_t fg_pid = smash.fg_pid;
    if (fg_pid == -1)
    {
        // if no process running, do nothing
        return;
    }
    if (kill(fg_pid, SIGKILL) == -1)
    {
        perror("smash error: kill failed");
    }
    else
    {
        cout << "smash: process " << fg_pid << " was killed" << endl;
        smash.jobs.removeJobById(fg_pid);
    }
    smash.fg_pid = -1;
}
