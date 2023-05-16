#include <stdio.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>

static int toy_timer;
static struct timeval start;

int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}

static void handler(int sig) { 
    struct timeval cur;
    double elapsed;

    gettimeofday(&cur, NULL);
    elapsed = (cur.tv_sec - start.tv_sec);
    elapsed += (cur.tv_usec - start.tv_usec) / 1000000.0;

    printf("Time elapse :%6.2fs\n", elapsed);
}

int system_server()
{
    struct itimerval itv;
    struct sigaction sa;

    printf("나 system_server 프로세스!\n");

    /* 5초 타이머를 만들어 봅시다. */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1)
        perror("sigaction");

    itv.it_value.tv_sec = 0;
    itv.it_value.tv_usec = 1;
    itv.it_interval.tv_sec = 5;
    itv.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
        perror("setitimer");

    gettimeofday(&start, NULL);

    while (1) {
        posix_sleep_ms(5000);
    }

    return 0;
}

int create_system_server()
{
    pid_t systemPid;
    const char *name = "system_server";

    printf("여기서 시스템 프로세스를 생성합니다.\n");

    /* fork 를 이용하세요 */
    switch (systemPid = fork()) {
    case -1:
        printf("fork failed\n");
    case 0:
        /* 프로세스 이름 변경 */
        if (prctl(PR_SET_NAME, (unsigned long) name) < 0)
            perror("prctl()");
        system_server();
        break;
    default:
        break;
    }

    return 0;
}
