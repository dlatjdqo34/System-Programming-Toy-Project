#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <mqueue.h>
#include <sys/inotify.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/sysmacros.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <camera_HAL.h>
#include <toy_message.h>
#include <shared_memory.h>

#define BUF_LEN 1024
#define TOY_TEST_FS "./fs"

pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  system_loop_cond  = PTHREAD_COND_INITIALIZER;
bool            system_loop_exit = false;    ///< true if main loop should exit

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

static int toy_timer = 0;
pthread_mutex_t toy_timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t global_timer_sem;
static bool global_timer_stopped;

static shm_sensor_t *the_sensor_info = NULL;
void set_periodic_timer(long sec_delay, long usec_delay);

static void timer_expire_signal_handler()
{
    // signal 문맥에서는 비동기 시그널 안전 함수(async-signal-safe function) 사용
    // man signal 확인
    // sem_post는 async-signal-safe function
    // 여기서는 sem_post 사용
    sem_post(&global_timer_sem);
}

static void system_timeout_handler()
{
    // 여기는 signal hander가 아니기 때문에 안전하게 mutex lock 사용 가능
    pthread_mutex_lock(&toy_timer_mutex);
    toy_timer++;
    pthread_mutex_unlock(&toy_timer_mutex);
}

static void *timer_thread(void *not_used)
{
    signal(SIGALRM, timer_expire_signal_handler);
    set_periodic_timer(1, 1);

	while (!global_timer_stopped) {
		int rc = sem_wait(&global_timer_sem);
		if (rc == -1 && errno == EINTR) {
		    continue;
		}

		if (rc == -1) {
		    perror("sem_wait");
		    exit(-1);
		}
        // 아래 sleep을 sem_wait 함수를 사용하여 동기화 처리
        // sleep(1);
		system_timeout_handler();
	}
	return 0;
}

void set_periodic_timer(long sec_delay, long usec_delay)
{
	struct itimerval itimer_val = {
		 .it_interval = { .tv_sec = sec_delay, .tv_usec = usec_delay },
		 .it_value = { .tv_sec = sec_delay, .tv_usec = usec_delay }
    };

	setitimer(ITIMER_REAL, &itimer_val, (struct itimerval*)0);
}

int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}

void *watchdog_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;

    printf("%s", s);

    while (1) {
        mqretcode = (int)mq_receive(watchdog_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("watchdog_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
    }

    return 0;
}

#define SENSOR_DATA 1

void *monitor_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;
    int shmid;

    printf("%s", s);

    while (1) {
        mqretcode = (int)mq_receive(monitor_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("monitor_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
        if (msg.msg_type == SENSOR_DATA) {
            shmid = msg.param1;
            the_sensor_info = toy_shm_attach(shmid);
            printf("sensor temp: %d\n", the_sensor_info->temp);
            printf("sensor info: %d\n", the_sensor_info->press);
            printf("sensor humidity: %d\n", the_sensor_info->humidity);
            toy_shm_detach(the_sensor_info);
        }
    }

    return 0;
}

long get_dir_size(char *path) { 
    DIR *dir; 
    struct dirent *dp;
    struct stat st;
    long total = 0;
    char file_path[1024];

    dir = opendir(path);

    for (;;) {
        dp = readdir(dir);
        if (dp == NULL) 
            break;

        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        sprintf(file_path, "%s/%s", path, dp->d_name);

        stat(dp->d_name, &st);
        if(S_ISDIR(st.st_mode)) {
            total += get_dir_size(file_path) + st.st_size;
        } else {
            total += st.st_size;
        }
    }

    return total;
}

void *disk_service_thread(void* arg)
{
    char *s = arg;
    int inotifyFd, wd, j;
    char buf[BUF_LEN] __attribute__ ((aligned(8)));
    ssize_t numRead;
    char *p;
    struct inotify_event *event;
    char *directory = TOY_TEST_FS;
    long total_size;
    struct stat st;

    printf("%s", s);

    // 여기에 구현
    inotifyFd = inotify_init();
    assert(inotifyFd != -1);

    wd = inotify_add_watch(inotifyFd, directory, IN_CREATE);
    assert(wd != -1);

    while (1) {
        numRead = read(inotifyFd, buf, BUF_LEN);
        assert(numRead > 0);

        printf("Read %ld bytes from inotify fd\n", (long)numRead);

        total_size = get_dir_size(TOY_TEST_FS);

        stat(directory, &st);

        printf("directory size: %ld\n", total_size);
        printf("File type: ");
        if (S_ISREG(st.st_mode)) printf("regular file\n");
        else if (S_ISDIR(st.st_mode)) printf("directory\n");
        else printf("other file\n");
        printf("I-node number: %ld\n", st.st_ino);
        printf("Number of (hard) links: %ld\n", st.st_nlink);
        printf("Ownership : UID=%d GID=%d\n", st.st_uid, st.st_gid);
        printf("File size: %ld\n", st.st_size);
        printf("Optimal I/O block size: %ld\n", st.st_blksize);  
        printf("Last file access: %ld\n", st.st_atime);
        printf("Last file modification: %ld\n", st.st_mtime);
        printf("Last status change: %ld\n", st.st_ctime);

    }

    return 0;
}

#define CAMERA_TAKE_PICTURE 1

void *camera_service_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;

    printf("%s", s);

   toy_camera_open();

    while (1) {
        mqretcode = (int)mq_receive(camera_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("camera_service_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
        if (msg.msg_type == CAMERA_TAKE_PICTURE) {
            toy_camera_take_picture();
        }
    }

    return 0;
}

void signal_exit(void)
{
    pthread_mutex_lock(&system_loop_mutex);
    system_loop_exit = true;
    pthread_cond_broadcast(&system_loop_cond);
    pthread_mutex_unlock(&system_loop_mutex);
}

int system_server()
{
    struct itimerspec ts;
    struct sigaction  sa;
    struct sigevent   sev;
    timer_t *tidlist;
    int retcode;
    pthread_t watchdog_thread_tid, monitor_thread_tid, disk_service_thread_tid, camera_service_thread_tid;
    pthread_t timer_thread_tid;

    printf("나 system_server 프로세스!\n");


    /* 메시지 큐를 오픈한다. */
    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    assert(watchdog_queue != -1);
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    assert(monitor_queue != -1);
    disk_queue = mq_open("/disk_queue", O_RDWR);
    assert(disk_queue != -1);
    camera_queue = mq_open("/camera_queue", O_RDWR);
    assert(camera_queue != -1);

    /* 스레드를 생성한다. */
    retcode = pthread_create(&watchdog_thread_tid, NULL, watchdog_thread, "watchdog thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&monitor_thread_tid, NULL, monitor_thread, "monitor thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&disk_service_thread_tid, NULL, disk_service_thread, "disk service thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&camera_service_thread_tid, NULL, camera_service_thread, "camera service thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&timer_thread_tid, NULL, timer_thread, "timer thread\n");
    assert(retcode == 0);

    printf("system init done.  waiting...");

    // 여기에 구현하세요... 여기서 cond wait로 대기한다. 10초 후 알람이 울리면 <== system 출력
    pthread_mutex_lock(&system_loop_mutex);
    while (system_loop_exit == false) {
        pthread_cond_wait(&system_loop_cond, &system_loop_mutex);
    }
    pthread_mutex_unlock(&system_loop_mutex);

    printf("<== system\n");

    while (1) {
        sleep(1);
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
