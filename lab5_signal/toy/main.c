#include <stdio.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <signal.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>

/* 여기서 sigchld 핸들러 구현 */
static void
sigchldHandler(int sig)
{
    /* 구현 */
    printf("handler: Caught SIGCHLD: %d\n", sig);
    printf("handler: returning\n");
}

int main()
{
    pid_t spid, gpid, ipid, wpid;
    int status, savedErrno;
    int sigCnt;
    sigset_t blockMask, emptyMask;
    struct sigaction sa;

    /* 여기서 SIGCHLD 시그널  등록 */
    sa.sa_flags = 0;
    sa.sa_handler = sigchldHandler;
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGCHLD, &sa, NULL) == -1) {
        fprintf(stderr, "signal registration error\n");
        exit(EXIT_FAILURE);
    }

    printf("메인 함수입니다.\n");
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
