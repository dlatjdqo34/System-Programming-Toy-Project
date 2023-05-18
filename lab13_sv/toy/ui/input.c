#include <assert.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <execinfo.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mqueue.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <execinfo.h>
#include <toy_message.h>
#include <shared_memory.h>

#define TOY_TOK_BUFSIZE 64
#define TOY_TOK_DELIM " \t\r\n\a"
#define TOY_BUFFSIZE 1024

typedef struct _sig_ucontext {
    unsigned long uc_flags;
    struct ucontext *uc_link;
    stack_t uc_stack;
    struct sigcontext uc_mcontext;
    sigset_t uc_sigmask;
} sig_ucontext_t;

static pthread_mutex_t global_message_mutex  = PTHREAD_MUTEX_INITIALIZER;
static char global_message[TOY_BUFFSIZE];

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;
static shm_sensor_t *the_sensor_info = NULL;
int shm_id[SHM_KEY_MAX - SHM_KEY_BASE];

void segfault_handler(int sig_num, siginfo_t * info, void * ucontext) {
  void * array[50];
  void * caller_address;
  char ** messages;
  int size, i;
  sig_ucontext_t * uc;

  uc = (sig_ucontext_t *) ucontext;

  /* Get the address at the time the signal was raised */
  caller_address = (void *) uc->uc_mcontext.rip;  // RIP: x86_64 specific     arm_pc: ARM

  fprintf(stderr, "\n");

  if (sig_num == SIGSEGV)
    printf("signal %d (%s), address is %p from %p\n", sig_num, strsignal(sig_num), info->si_addr,
           (void *) caller_address);
  else
    printf("signal %d (%s)\n", sig_num, strsignal(sig_num));

  size = backtrace(array, 50);
  /* overwrite sigaction with caller's address */
  array[1] = caller_address;
  messages = backtrace_symbols(array, size);

  /* skip first stack frame (points here) */
  for (i = 1; i < size && messages != NULL; ++i) {
    printf("[bt]: (%d) %s\n", i, messages[i]);
  }

  free(messages);

  exit(EXIT_FAILURE);
}

/*
 *  sensor thread
 */
void *sensor_thread(void* arg)
{
    int mqretcode;
    char *s = arg;
    toy_msg_t msg;
    // 여기 추가: 공유메모리 키
    shm_sensor_t shmsg;

    printf("%s", s);

    while (1) {
        posix_sleep_ms(5000);
        // 여기에 구현해 주세요.
        // 현재 고도/온도/기압 정보를  SYS V shared memory에 저장 후
        // monitor thread에 메시지 전송한다.
        shmsg.temp = 22;
        shmsg.press = 1;
        shmsg.humidity = 62;

        memcpy(the_sensor_info, &shmsg, sizeof(shm_sensor_t));

        msg.msg_type = 1;
        msg.param1 = shm_id[0];
        msg.param2 = 0;
        mqretcode = mq_send(monitor_queue, (char *)&msg, sizeof(msg), 0);
        assert(mqretcode == 0);
    }

    return 0;
}
 
/*
 *  command thread
 */

int toy_send(char **args);
int toy_mutex(char **args);
int toy_shell(char **args);
int toy_message_queue(char **args);
int toy_exit(char **args);

char *builtin_str[] = {
    "send",
    "mu",
    "sh",
    "mq",
    "exit"
};

int (*builtin_func[]) (char **) = {
    &toy_send,
    &toy_mutex,
    &toy_shell,
    &toy_message_queue,
    &toy_exit
};

int toy_num_builtins()
{
    return sizeof(builtin_str) / sizeof(char *);
}

int toy_send(char **args)
{
    printf("send message: %s\n", args[1]);

    return 1;
}

int toy_mutex(char **args)
{
    if (args[1] == NULL) {
        return 1;
    }

    printf("save message: %s\n", args[1]);
    pthread_mutex_lock(&global_message_mutex);
    strcpy(global_message, args[1]);
    pthread_mutex_unlock(&global_message_mutex);
    return 1;
}

int toy_message_queue(char **args)
{
    int mqretcode;
    toy_msg_t msg;

    if (args[1] == NULL || args[2] == NULL) {
        return 1;
    }

    if (!strcmp(args[1], "camera")) {
        msg.msg_type = atoi(args[2]);
        msg.param1 = 0;
        msg.param2 = 0;
        mqretcode = mq_send(camera_queue, (char *)&msg, sizeof(msg), 0);
        assert(mqretcode == 0);
    }

    return 1;
}

int toy_exit(char **args)
{
    return 0;
}

int toy_shell(char **args)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0) {
        if (execvp(args[0], args) == -1) {
            perror("toy");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("toy");
    } else {
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

int toy_execute(char **args)
{
    int i;

    if (args[0] == NULL) {
        return 1;
    }

    for (i = 0; i < toy_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    return 1;
}

char *toy_read_line(void)
{
    char *line = NULL;
    ssize_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            exit(EXIT_SUCCESS);
        } else {
            perror(": getline\n");
            exit(EXIT_FAILURE);
        }
    }
    return line;
}

char **toy_split_line(char *line)
{
    int bufsize = TOY_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token, **tokens_backup;

    if (!tokens) {
        fprintf(stderr, "toy: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOY_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += TOY_TOK_BUFSIZE;
            tokens_backup = tokens;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                free(tokens_backup);
                fprintf(stderr, "toy: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOY_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

void toy_loop(void)
{
    char *line;
    char **args;
    int status;

    do {
        printf("TOY>");
        line = toy_read_line();
        args = toy_split_line(line);
        status = toy_execute(args);
        free(line);
        free(args);
    } while (status);
}

void *command_thread(void* arg)
{
    char *s = arg;

    printf("%s", s);

    toy_loop();

    return 0;
}

int input()
{
    int retcode;
    struct sigaction sa;
    pthread_t command_thread_tid, sensor_thread_tid;
    int i;

    printf("나 input 프로세스!\n");

    memset(&sa, 0, sizeof(sigaction));
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = segfault_handler;

    sigaction(SIGSEGV, &sa, NULL); /* ignore whether it works or not */

    /* 센서 정보를 공유하기 위한, 시스템 V 공유 메모리를 생성한다 */
    // 여기에 구현해주세요....
    shm_id[SHM_KEY_SENSOR - SHM_KEY_BASE] =
        shmget(SHM_KEY_SENSOR, sizeof(shm_sensor_t), IPC_CREAT | 0666);
    if(shm_id[0] == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }
    the_sensor_info = (shm_sensor_t *)shmat(shm_id[0], NULL, 0);
    if(the_sensor_info == (void *)-1){
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    /* 메시지 큐를 오픈 한다.
     * 하지만, 사실 fork로 생성했기 때문에 파일 디스크립터 공유되었음. 따따서, extern으로 사용 가능
    */
    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    assert(watchdog_queue != -1);
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    assert(monitor_queue != -1);
    disk_queue = mq_open("/disk_queue", O_RDWR);
    assert(disk_queue != -1);
    camera_queue = mq_open("/camera_queue", O_RDWR);
    assert(camera_queue != -1);

   /* 여기서 스레드를 생성한다. */
    retcode = pthread_create(&command_thread_tid, NULL, command_thread, "command thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&sensor_thread_tid, NULL, sensor_thread, "sensor thread\n");
    assert(retcode == 0);

    while (1) {
        sleep(1);
    }

    return 0;
}

int create_input()
{
    pid_t systemPid;
    const char *name = "input";

    printf("여기서 input 프로세스를 생성합니다.\n");

    /* fork 를 이용하세요 */
    switch (systemPid = fork()) {
    case -1:
        printf("fork failed\n");
    case 0:
        /* 프로세스 이름 변경 */
        if (prctl(PR_SET_NAME, (unsigned long) name) < 0)
            perror("prctl()");
        input();
        break;
    default:
        break;
    }

    return 0;
}