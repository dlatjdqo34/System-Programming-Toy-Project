#include <stdio.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>

int create_web_server()
{
    pid_t systemPid;

    printf("여기서 Web Server 프로세스를 생성합니다.\n");


    // fork를 clone + 새로운 namespace로 생성하세요.
    switch (systemPid = fork()) {
    case -1:
        printf("fork failed\n");
    case 0:
        if (execl("/usr/local/bin/filebrowser", "filebrowser", "-p", "8282", (char *) NULL)) {
            printf("execfailed\n");
        }
        break;
    default:
        break;
    }

    return 0;
}