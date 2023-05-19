#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>

#define STACK_SIZE (1024 * 1024)

static int child_web_server(void *arg)
{
    printf("Clone Web Server Succeed!\n");

    printf("[%d] Current namespace, Parent PID : %d\n", getpid(), getppid());

    if (execl("/usr/local/bin/filebrowser", "filebrowser", "-p", "8282",
              (char *)NULL)) {
        printf("execfailed\n");
        return 1;
    }

    return 0;
}

int create_web_server()
{
    pid_t systemPid;
    char *stack;
    int flags = 0;

    printf("여기서 Web Server 프로세스를 생성합니다.\n");


    // fork를 clone + 새로운 namespace로 생성하세요.
    stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    flags = CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD;

    systemPid = clone(child_web_server, stack + STACK_SIZE, flags, NULL);
    if (systemPid == -1) {
        perror("clone");
        exit(EXIT_FAILURE);
    }

    return 0;
}
