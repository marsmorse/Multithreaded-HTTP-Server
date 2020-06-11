#include  "requestQueue.h"
#include <pthread.h>
#include <stdlib.h>


typedef struct RequestObj{
    int log_fd;
    int client_socketfd;
    struct RequestObj* next;
} RequestObj;

typedef struct RequestQueueObj{
    Request head;
    Request tail;
    int numReqs;
} RequestQueueObj;


Request getRequest(RequestQueue this,pthread_mutex_t* prgMutex){
        __uint8_t retCode;
        Request aRequest;
        if(this->numReqs > 0){
            aRequest = this->head;
            this->head = aRequest->next;
            if(this->head ==NULL){
                this->tail=NULL;
            }
            this->numReqs--;
        }else{
            aRequest = NULL;
        }
        retCode = pthread_mutex_unlock(prgMutex);
        if(retCode != 0){
                
        }
        return aRequest;
        }
    void pushRequest(RequestQueue this,int client_socketd, int log_fd, pthread_mutex_t* prgMutex, pthread_cond_t* prgCondVar){
            __uint8_t retCode;
            Request aRequest = (Request)malloc(sizeof(RequestObj));
            if(!aRequest){
                
            }
            aRequest->client_socketfd = client_socketd;
            aRequest->log_fd = log_fd;
            aRequest->next = NULL;
            
            retCode = pthread_mutex_lock(prgMutex);
            if(this->numReqs == 0){
                this->head = aRequest;
                this->tail = aRequest;
            }
            else{
                this->tail->next = aRequest;
                this->tail = aRequest;
            }
            this->numReqs++;
            retCode = pthread_mutex_unlock(prgMutex);
            retCode = pthread_cond_signal(prgCondVar);
            if(retCode != 0){
                
            }
            aRequest = NULL;
            
        }
int getRequestNumber(RequestQueue this){
    return this->numReqs;
}
int getClientSockd(Request this){
    return this->client_socketfd;
}
    int getLogFD(Request this){
        return this->log_fd;
    }
//constructors


RequestQueue newRequestQueue(void){
    RequestQueue RQ = malloc(sizeof(RequestQueueObj));
    RQ->head = NULL;
    RQ->tail = NULL;
    RQ->numReqs = 0;
    return RQ;
    }