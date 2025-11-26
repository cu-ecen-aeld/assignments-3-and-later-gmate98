#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

// Conversion betweem us and ms 
#define CONV_MS_TO_US 1000

void* threadfunc(void* thread_param)
{
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    thread_func_args->thread_complete_success = true;
    
    usleep(thread_func_args->wait_time_obt_mx_ms*CONV_MS_TO_US);
    
    if(pthread_mutex_lock(thread_func_args->mutex)) {
        perror("pthread_mutex_lock");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }
    
    usleep(thread_func_args->wait_time_rel_mx_ms*CONV_MS_TO_US);
    
    if(pthread_mutex_unlock(thread_func_args->mutex)) {
        perror("pthread_mutex_unlock");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    struct thread_data* tdata;
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    tdata = calloc(1,sizeof(struct thread_data));
    if(tdata == NULL){
        perror("calloc");
        return false;
    }

    // Initilaize thread_data
    tdata->wait_time_obt_mx_ms = wait_to_obtain_ms;
    tdata->wait_time_rel_mx_ms = wait_to_release_ms;
    tdata->mutex = mutex;
    tdata->thread_complete_success = false;

    // Create thread and pass thread parameters to start routine
    if(pthread_create(thread, NULL, threadfunc, (void *)tdata)) {
        free(tdata);
        perror("pthread_create");
        return false;
    }

    return true;
}

