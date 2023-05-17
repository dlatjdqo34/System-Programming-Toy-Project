#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <assert.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <toy_message.h>

#define NUM_MESSAGES 10

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

static void
sigchldHandler(int sig)
{
    int status, savedErrno;
    pid_t childPid;

    savedErrno = errno;

    printf("handler: Caught SIGCHLD : %d\n", sig);

    while ((childPid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("handler: Reaped child %ld - ", (long) childPid);
        (NULL, status);
    }

    if (childPid == -1 && errno != ECHILD)
        printf("waitpid");

    printf("handler: returning\n");

    errno = savedErrno;
}

int main()
{
    pid_t spid, gpid, ipid, wpid;
    int status, savedErrno;
    int sigCnt;
    sigset_t blockMask, emptyMask;
    struct sigaction sa;
    int retcode;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigchldHandler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        printf("sigaction");
        return 0;
    }

    printf("메인 함수입니다.\n");

    // 여기에 구현하세요.
    /* 메시지 큐를 생성한다. */
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(toy_msg_t);

    watchdog_queue   = mq_open("/watchdog_queue", O_CREAT, 0666, &attr);
    assert(watchdog_queue != (mqd_t)-1);
    monitor_queue = mq_open("/monitor_queue", O_CREAT, 0666, &attr);
    assert(monitor_queue != (mqd_t)-1);
    disk_queue       = mq_open("/disk_queue", O_CREAT, 0666, &attr);
    assert(disk_queue != (mqd_t)-1);
    camera_queue     = mq_open("/camera_queue", O_CREAT, 0666, &attr);
    assert(camera_queue != (mqd_t)-1);

    printf("시스템 서버를 생성합니다.\n");
    spid = create_system_server();
    printf("웹 서버를 생성합니다.\n");
    wpid = create_web_server();
    printf("입력 프로세스를 생성합니다.\n");
    ipid = create_input();
    printf("GUI를 생성합니다.\n");
    gpid = create_gui();

    waitpid(spid, &status, 0);
    waitpid(gpid, &status, 0);
    waitpid(ipid, &status, 0);
    waitpid(wpid, &status, 0);

    return 0;
}
