

#include <pthread.h>

typedef struct RequestObj* Request;
typedef struct RequestQueueObj* RequestQueue;

    Request getRequest(RequestQueue this, pthread_mutex_t* prgMutex);
    void pushRequest(RequestQueue this, int client_socketd, int log_fd, pthread_mutex_t* prgMutex, pthread_cond_t* prgCondVar);
    RequestQueue newRequestQueue();
    int getRequestNumber(RequestQueue this);
    int getClientSockd(Request this);
    int getLogFD(Request this);
//expects all requests being pushed to it to be no bigger thatn 4096
