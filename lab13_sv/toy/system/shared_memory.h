#ifndef _SHARED_MEMORY_H
#define _SHARED_MEMORY_H

#include <sys/shm.h> 
#include <sys/ipc.h> 

enum def_shm_key {
    SHM_KEY_BASE=10,
    SHM_KEY_SENSOR = SHM_KEY_BASE,
    SHM_KEY_MAX
};

typedef struct shm_sensor {
    int temp;
    int press;
    int humidity;
} shm_sensor_t;

extern int shm_id[SHM_KEY_MAX - SHM_KEY_BASE];

#endif /* _SHARED_MEMORY_H */
