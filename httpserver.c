#include <sys/socket.h>
#include <getopt.h>
#include <sys/stat.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <unistd.h> // write
#include <string.h> // memset
#include <stdlib.h> // atoi
#include <stdbool.h> // true, false
#include <errno.h>
#include <pthread.h>
#include "requestQueue.h"
#include <stdatomic.h>
#define UINT8 u_int8_t
#define BUFFER_SIZE 16080
#define MAX_HEADER_SIZE 4096
//response codes
#define OK 200
#define CREATED 201
#define ERROR -1
#define BAD_REQ 400
#define NOT_FOUND 404
#define FORBIDDEN 403
#define INTERNAL_SERVER_ERROR 500

//request MUTEX
pthread_mutex_t req_mutex = PTHREAD_MUTEX_INITIALIZER;

//request condition variable
pthread_cond_t got_request = PTHREAD_COND_INITIALIZER;

//Log MUTEX
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

//log condition variable
pthread_cond_t log_request = PTHREAD_COND_INITIALIZER;
atomic_uint_fast32_t offset = 0;
struct httpObject {
    /*
        Create some object 'struct' to keep track of all
        the components related to a HTTP message
        NOTE: There may be more member variables you would want to add
    */
    char method[5];         // PUT, HEAD, GET
    char filename[28];      // what is the file we are worried about
    char httpversion[9];    // HTTP/1.1
    size_t content_length; // example: 13
    int status_code;
    char bad_filename[55];
    char bad_httpversion[20];
    char bad_method[15];
    
};
atomic_int failures = 0;
atomic_int successes = 0;

void writeLogBody(size_t lineCount, UINT8 remainder_line_length, int fd, int log_fd, size_t* thread_offset, size_t* binary_index_count){
    UINT8 buf[BUFFER_SIZE];
    char binary_index_line[9];
    strcpy(binary_index_line,"00000000");
    
    ssize_t readSize;
    char binary_index[9];
//read and write full lines
    if(lineCount == 0){
        readSize = read(fd, buf, 20);
    }else{
        //printf("Line count is %d\n", lineCount);
        while(lineCount > 0){
            readSize = read(fd, buf, BUFFER_SIZE);
            //printf("read size is %ld\n", readSize);
            int j = 0;
            for(int i = 0; i< readSize/20;i++, j+=20, (*thread_offset) += 69,(*binary_index_count)+=20){
                //read a 
                //check for overflow
                if((*binary_index_count) == 100000000){
                    (*binary_index_count)= 0;
                    strcpy(binary_index_line,"00000000");
                }
                
                snprintf(binary_index, 8, "%lu", *binary_index_count);
                //printf("binary_index: %s\n", binary_index);
                //printf("length of binary index is %ld\n", strlen(binary_index));
                for(UINT8 k = 0; k < strlen(binary_index); k++){
                    binary_index_line[(8-strlen(binary_index) + k)] = binary_index[k];
                }
                char log_line[70];
                strcpy(log_line, binary_index_line);
                //printf("binary_index_line: %s\n", binary_index_line);
                char hex_line_conversion[61];
                snprintf(hex_line_conversion, 62, " %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    buf[j], buf[j + 1], buf[j + 2], buf[j + 3], buf[j + 4], buf[j + 5],
                    buf[j + 6], buf[j + 7], buf[j + 8], buf[j + 9], buf[j + 10],
                    buf[j + 11], buf[j + 12], buf[j + 13], buf[j + 14], buf[j + 15], 
                    buf[j + 16], buf[j + 17], buf[j + 18], buf[j + 19] 
                );
                strcat(log_line, hex_line_conversion);
                //printf("%s", log_line);
                pwrite(log_fd,log_line,strlen(log_line), (*thread_offset));
    
                        }
            lineCount -= readSize/20;
        }
    }
//process remaining lines
    //printf("\n\nREMAINDER TESTING\n\n");
    //printf("remainder line length is %d\n", remainder_line_length);
    if(remainder_line_length>0){
        buf[remainder_line_length] = '\0';
        if((*binary_index_count) == 100000000){
            (*binary_index_count)= 0;
        }
        snprintf(binary_index, 9, "%08ld", *binary_index_count);
        //printf("binary index is: %s\n", binary_index);
        for(UINT8 i = 0; i < strlen(binary_index); i++){
            binary_index_line[(8-strlen(binary_index) + i)] = binary_index[i];
        }
        char log_line[79];
        strcpy(log_line, binary_index_line);
    
        char hex_line_conversion[4];
        for(ssize_t i = readSize - remainder_line_length; i <readSize; i++){
            snprintf(hex_line_conversion, 4, " %02x", buf[i]);
            strcat(log_line, hex_line_conversion);
        }
        strcat(log_line, "\n");
        //printf("thread offset is %ld", *thread_offset);
        //printf("log line is %s\n", log_line);
        pwrite(log_fd, log_line, strlen(log_line), (*thread_offset));
        (*thread_offset) += (strlen(log_line));
        //asser((*thread_offset))
        //printf("thread offset is: %ld after writing a line\n", (*thread_offset));
    }
    
}
void handleLog(struct httpObject* message, int log_fd){
    //create first line of log and get size
    UINT8 buf[200];
    char firstLine[100];
    ssize_t fd = open(message->filename, O_RDONLY);
    size_t thread_offset;
    //if the message is a legit PUT or GET
    if((message->status_code == OK || message->status_code == CREATED) && strcmp(message->method, "HEAD")!= 0){
        sprintf(firstLine, "%s /%s length %ld\n", message->method, message->filename, message->content_length);
        //printf("the length of the first line is %lu", strlen(firstLine));
        //calculate the message length by first calculating size of full lines then adding size of the last line then adding the size of the first line then the terminating string
        size_t log_message_length = strlen(firstLine) + (69*(message->content_length/20)) + 9;
        if((message->content_length % 20) > 0){
            log_message_length += (8 + ((message->content_length % 20)*3) + 1);
        } 
        //lock otherthreads so we can log this request with multiply calls to pwrite and write the full log no interruptions
        size_t count = 0;
        thread_offset = __atomic_fetch_add(&offset,log_message_length, __ATOMIC_RELAXED);
        //printf("thread offset is: %ld before processing\n", thread_offset);
        //printf("first line is: %s\n", firstLine);
        pwrite(log_fd, firstLine, strlen(firstLine), thread_offset);
        thread_offset += strlen(firstLine);
        //printf("thread offset is: %ld after writing first line\n", thread_offset);
        size_t numberOfFullLines = message->content_length/20;
        UINT8 remainder_line_length = message->content_length % 20;
        //printf("number of full lines %d\n", numberOfFullLines);
        //printf("content length: %ld\n", message->content_length);
        writeLogBody(numberOfFullLines,remainder_line_length, fd, log_fd, &thread_offset, &count);
        
        //printf("Thread offset before terminating string is at %lu\n", thread_offset);
        char terminating_str[10];
        strcpy(terminating_str, "========\n");
        pwrite(log_fd, terminating_str, strlen(terminating_str), thread_offset);
        thread_offset += strlen(terminating_str);
        //printf("\nfinal Thread offset is at %lu and global offset is %u\n", thread_offset, offset);
        __atomic_fetch_add(&successes, 1,__ATOMIC_RELAXED);
        if(close(fd) == -1 ){
            fprintf(stderr , "close failed: %s\n", strerror(errno));
        } 

    }
    else if(message->status_code == OK && strcmp(message->method, "HEAD")==0){
        sprintf(firstLine, "%s /%s length %ld\n", message->method, message->filename,  message->content_length);
        strcpy((char*)buf, firstLine);
        strcat((char*)buf, "========\n");
        thread_offset = __atomic_fetch_add(&offset, strlen(firstLine) + 9, __ATOMIC_RELAXED); 
        pwrite(log_fd, buf ,strlen(firstLine) + 9,thread_offset); 
        __atomic_fetch_add(&successes, 1,__ATOMIC_RELAXED);
    } else{
        //printf("filename at time of logging is %s\nand has a length of %lu\n", message->bad_filename, strlen(message->bad_filename));
        char temp_file_name[strlen(message->bad_filename)];
        strcpy(temp_file_name, message->bad_filename);
        sprintf(firstLine, "FAIL: %s /%s %s --- response %d\n", message->bad_method, temp_file_name, message->bad_httpversion, message->status_code);
        //printf("first line is %s", firstLine);
        strcpy((char*)buf, firstLine);
        strcat((char*)buf, "========\n");
        //printf("buffer is %s\n", buf);
        thread_offset = __atomic_fetch_add(&offset, strlen(firstLine)+9, __ATOMIC_RELAXED); 
        pwrite(log_fd, buf ,strlen(firstLine) + 9,thread_offset);  
        __atomic_fetch_add(&failures, 1,__ATOMIC_RELAXED);
    }
       

    //calculate length of the log body including the terminating =====\n
}

/*---------------

permission check functions

-------------------*/
int checkReadPerms(char* filePath){
    struct stat fileInfo;
    if(stat(filePath, &fileInfo) == -1){
        if(errno == ENOENT){
            return NOT_FOUND;
        }
                fprintf(stderr , "[checkReadPerms] fstat failed to get info from file: %s\n", strerror(errno));
                return INTERNAL_SERVER_ERROR;
    }else{
                if((fileInfo.st_mode & S_IRUSR)){
                    return OK;
                }else{
                    //printf("[checkReadPerms] no read access for requested file\n");
                    return FORBIDDEN;
                }
  
                
    }
}
int checkWritePerms(char* filePath){
    struct stat fileInfo;
    if(stat(filePath, &fileInfo) == -1){
                fprintf(stderr , "[checkWritePerms] fstat failed to get info from file: %s\n", strerror(errno));
                return INTERNAL_SERVER_ERROR;
    }else{
                //printf("[checkWritePerms] checking write access for file\n");
                if((fileInfo.st_mode & S_IWUSR)){
                    //printf("[checkWritePerms] user has right acces for requested file\n");
                    return OK;
                }else{
                    //printf("[checkWritePerms] no read access for requested file\n");
                    return FORBIDDEN;
                }
  
                
    }
}
int validateFileName(char* fileName){
    if(*fileName != '/'){
       // printf("no preceding /\n");
        return BAD_REQ;
    }else{
        fileName++;
        if(strlen(fileName)>27){
           // printf("file is too long\n");
            return BAD_REQ;
        }else{
            char* it = fileName;
            for(UINT8 i =0; i<strlen(fileName);i++,it++){
                if(!((*it > 64 && *it<91)||(*it > 96 && *it<123)||(*it > 47 && *it< 58)||*it==95||*it==45)){
                    //printf("invalid character in fileName: %c\n", *it);
                }
            }
            return OK;
        }
    }
    
    
}
void writeContentToSocket(ssize_t client_sockd, char* filePath){
    //printf("[writeContentToSocket] writing content to socket\n");
    ssize_t fd = open(filePath, O_RDONLY);
    if(fd == -1){
        fprintf(stderr , "[writeContentToSocket] open failed: %s\n", strerror(errno));
    }
    else{

       UINT8 buffer[BUFFER_SIZE];
       ssize_t readSize;
       //= read(fd, buffer, BUFFERSIZE);
        while((readSize = read(fd, buffer, sizeof(buffer)))  > 0){
            
            if(send(client_sockd, buffer, readSize,0) < 0){
                
                break;
            }
            //printf("buffer is: %s", buffer);
        }
        if(close(fd) == -1 ){
            fprintf(stderr , "close failed: %s\n", strerror(errno));
        }
        //printf("[writeContentToSocket] Done writing to socket\n");
    }
}
int handlePUTreq(int client_sockd, struct httpObject* message){
    //printf("[+] Handling PUT request\n");
    //struct stat fileStat;
    //S_IRWXU is the mode argument giving the uer read write and execute access
    ssize_t server_fd = open(message->filename, O_CREAT | O_RDWR | O_TRUNC, S_IRWXU);        
    //check write permissions for PUT request
        if(message->status_code ==OK && (strcmp(message->method,"PUT")== 0)){
            int access = checkWritePerms(message->filename);
            if(access==FORBIDDEN){
                return FORBIDDEN;
            }
            if(access==INTERNAL_SERVER_ERROR){
                return INTERNAL_SERVER_ERROR;
            }
        }
    if(server_fd == -1){
        if(errno == EACCES){
                return NOT_FOUND;
        }
        fprintf(stderr , "open failed: %s\n", strerror(errno));
        return INTERNAL_SERVER_ERROR;
    }else{

        char buffer[BUFFER_SIZE];
        size_t bytesReadTotal = 0;
        ssize_t readSize;
            while((readSize = read(client_sockd,buffer,BUFFER_SIZE))>0){
                bytesReadTotal += readSize;
                //printf("\nbuffer is %s\n",buffer);
                write(server_fd, buffer, readSize);
                if(bytesReadTotal>=message->content_length){
                    break;
                }
                //printf("readsize is %lu\n",readSize);
            }
            if(close(server_fd) == -1 ){
                fprintf(stderr , "close failed: %s\n", strerror(errno));
                return INTERNAL_SERVER_ERROR;
            }
        return CREATED;
    
        }


}
size_t getContentLengthFromFile(struct httpObject* message){
    ssize_t fd = open(message->filename, O_RDONLY|O_EXCL);
        if(fd == -1){
            if(errno == EACCES){
                message->status_code = FORBIDDEN;
            }else if(errno != EEXIST){
                message->status_code = NOT_FOUND;
            }else{
                message->status_code = INTERNAL_SERVER_ERROR;
            }
            fprintf(stderr , "open failed: %s\n", strerror(errno));
        }
        else{
            struct stat fileInfo;
            if(fstat(fd, &fileInfo) == -1){
                message->status_code = INTERNAL_SERVER_ERROR;
                fprintf(stderr , "fstat failed to get info from file descriptor: %s\n", strerror(errno));
            }else{
                return fileInfo.st_size;
            }
            if(close(fd) == -1 ){
            message->status_code = INTERNAL_SERVER_ERROR;
            fprintf(stderr , "close failed: %s\n", strerror(errno));
        }
        }
        
    return 0;
}
int validateReqLine(char* reqLine, struct httpObject* message, char* savePtr){
    char* token = strtok_r(reqLine," ",&savePtr);
    int status = OK;
    if(!(strcmp(token,"HEAD")==0||strcmp(token,"GET")==0||strcmp(token,"PUT")==0)){
        strcpy(message->method, token);
        strcpy(message->bad_method, token);
        status = BAD_REQ;
    }else{
        strcpy(message->method, token);
        strcpy(message->bad_method, token);
    }
    //-------
    //parse filename
    //-------
    token = strtok_r(NULL," ",&savePtr);
    int validationCode = validateFileName(token);
    //check fileName validity set status code accordingly
    token++;
    strcpy(message->bad_filename, token);  
    if(validationCode == OK){
        strcpy(message->filename, token);
    }else{
        status = BAD_REQ;
    }
    //-------
    //parse http version
    //-------

    token = strtok_r(NULL,"\r\n", &savePtr);
    strcpy(message->httpversion, token);
    strcpy(message->bad_httpversion, token);
    //printf("token is %s\n", token);
    if(strcmp(token, "HTTP/1.1")!=0){
        status = BAD_REQ;
        //printf("apparently token is http/1.1 %s\n", token);
    }
    return status;
}
void parseRequest(ssize_t client_sockd, struct httpObject* message){
    char buffer[MAX_HEADER_SIZE];
    ssize_t bytes = read(client_sockd, buffer, BUFFER_SIZE);
    if(bytes == -1){
        message->status_code = 500;
        fprintf(stderr , "failed to read from the socket: %s\n", strerror(errno));
    }else{

        char* savePtr;
        char* savePtr2;
        char* token= strtok_r(buffer,"\r\n",&savePtr);
        //printf("request header is %s", token);
        //parse request line

        message->status_code = validateReqLine(token, message, savePtr);

        UINT8 foundContentLength = false;
        token= strtok_r(NULL,"\r\n",&savePtr);
        //parse headers
        while(token!= NULL){
            //printf("[+] header line is %s\n",token);
            char* headerName = strtok_r(token,":",&savePtr2);
            //printf("[+] header name is %s\n",headerName);
            if(strcmp(headerName,"Content-Length")==0){
                
                char* headerValue = strtok_r(NULL,"\r\n", &savePtr2);
                //printf("[+] header value is %s\n",headerValue);
                message->content_length = atoi(headerValue);
                foundContentLength = true;
                break;
            }
            token= strtok_r(NULL,"\r\n",&savePtr);
        }
        if(!foundContentLength && strcmp(message->method,"PUT") ==0){
            //printf("[+]no content length header included in PUT request");
            message->status_code = BAD_REQ;
        }
        //------
        //parse content length for get requests then check file permsions
        //------
        if(message->status_code == OK && (strcmp(message->method,"HEAD") == 0 || strcmp(message->method,"GET") == 0)){
            message->status_code = checkReadPerms(message->filename);
            if(message->status_code == OK){
                message->content_length = getContentLengthFromFile(message);
            }
        }
    }
}

char* getStatusCodeName(int status_code){
    switch(status_code){
        case 200:
            return "OK";
        case 201:
            return "Created";   
        case 400:
            return "Bad Request";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        default:
            return "Internal Server Error";
    }
}

char* createResponseHeader( struct httpObject* message) {
    //printf("[createResponseHeader] status code = %d\n",message->status_code);
    char* StatusCodeName = getStatusCodeName(message->status_code);
    char* response = malloc(1500);
    // = malloc(600);
    size_t contentLength = 0;
    // if the request is a get request get the content length of the file
    if(message->status_code == OK &&((strcmp(message->method,"HEAD")==0)||(strcmp(message->method,"GET")==0))){
        contentLength = message->content_length;
    }
    sprintf(response,"%s %d %s\r\nContent-Length: %ld\r\n\r\n", message->httpversion, message->status_code,StatusCodeName, contentLength);
    //printf("response is %s\n", response);
    return response;
}
void writeHealthCheckResponse(int client_sockd, int log_fd, struct httpObject* message){
    char* StatusCodeName = getStatusCodeName(message->status_code);
    char response[70];
    int totalAttempts = successes + failures;
    UINT8 health[20];
    sprintf((char* )health, "%d\n%d",failures, totalAttempts);
    //printf("health line is: %s\n", (char*)health);
    health[strlen((char*)health)] = '\0';
   // printf("inside writeHealthCheckResponse\n");
    sprintf(response,"%s %d %s\r\nContent-Length: %ld\r\n\r\n%s\n", message->httpversion, message->status_code, StatusCodeName, strlen((char*)health),health);
    
    ssize_t bytesSent = send(client_sockd, response, strlen((char*)response),0);
    bytesSent = send(client_sockd, health, strlen((char*)health),0);
    if(bytesSent == -1){
        fprintf(stderr , "failed to send response to the client: %s\n", strerror(errno));
    }
    if(log_fd != -1){
        //assuming healthcheck size won't be > 20
        UINT8 health_bytes = strlen((char* )health);
        char firstLine[100];
        sprintf(firstLine, "%s %s length %ld\n", message->method, message->filename, strlen((char*)health));
        UINT8 log_message_length = strlen(firstLine) + (8 + (health_bytes*3) + 1) + 9;
        size_t thread_offset = __atomic_fetch_add(&offset, log_message_length, __ATOMIC_RELAXED);
        pwrite(log_fd, firstLine, strlen(firstLine), thread_offset);
        thread_offset += strlen(firstLine);
    
        //write to log assuming size of health check is smaller than 20 bytes : ()
        UINT8 binary_index_count = 0;
        char binary_index[9];
        snprintf(binary_index, 9, "%08d", binary_index_count);
        char log_line[85];
        strcpy(log_line, binary_index);
    
        char hex_line_conversion[4];
        for(size_t i = 0; i < health_bytes; i++){
            snprintf(hex_line_conversion, 4, " %02x", health[i]);
            strcat(log_line, hex_line_conversion);
        }
        strcat(log_line, "\n");
        pwrite(log_fd, log_line, strlen(log_line), thread_offset);
        thread_offset += (strlen(log_line));
        //printf("remainder line has been processed\n");
        char terminating_str[10];
        strcpy(terminating_str, "========\n");
        pwrite(log_fd, terminating_str, strlen(terminating_str), thread_offset);
        thread_offset += strlen(terminating_str);
        __atomic_fetch_add(&successes, 1,__ATOMIC_RELAXED);
    }
}
void writeResponse(ssize_t client_sockd, struct httpObject* message,char* response){
    //printf("[writeResponse] writing response:\n%s\r\n", response);
    ssize_t bytesSent = send(client_sockd, response, strlen(response),0);
    if(bytesSent == -1){
        fprintf(stderr , "failed to send response to the client: %s\n", strerror(errno));
    }else{
        if(strcmp(message->method,"GET")==0 && message->status_code == OK){
            writeContentToSocket(client_sockd, message->filename);
        }
    }
}
void handleRequest(int client_sockd, int log_fd){
    //set text color to red
        //printf("\033[0;31m");
        //printf("handleRequest() has been called\n");
        /*
         * 1. create message object
         */
        struct httpObject message;
        /*
         * 2. parse request
         */
        
        /*
         * 2. If request is a valid PUT request handle it
         */
        
        /*
         * 2. if its a healthcheck Get request write special response
         */
        parseRequest(client_sockd, &message);
        //printf("  status code: %d\n",message.status_code);
        if(strcmp(message.filename,"healthcheck")==0 && strcmp(message.method,"GET")== 0 && (message.status_code == OK || message.status_code == NOT_FOUND) ){
                //printf("Writing health check response\n");
                
                writeHealthCheckResponse(client_sockd,log_fd, &message);
                //printf("finished Writing health check response\n");
        }else{
            if(strcmp(message.filename,"healthcheck") == 0 && log_fd == -1){
                message.status_code = NOT_FOUND;
            }else if(strcmp(message.filename,"healthcheck")==0 && strcmp(message.method,"GET")!= 0){
                message.status_code = FORBIDDEN;
            }
            if(strcmp(message.method, "PUT")==0 && message.status_code==OK){
                message.status_code = handlePUTreq(client_sockd,&message);
                //printf("status code:%d\n",message.status_code);
            }
        /*
         * 3. createResponse
         */
            char* response = createResponseHeader(&message);
        /*
         * 2. write response to socket
         */
            writeResponse(client_sockd, &message, response);
            free(response);
            close(client_sockd);        
           // printf("\033[0;34m");
            if(log_fd != -1){
                handleLog(&message, log_fd);
            }
        }
        
        
        //printf("\033[0m");
}
void* handleRequests(void* data){
    //printf("\033[0;32m[#] A new thread has called handleRequests\n\033[0m");
    RequestQueue rq = ((RequestQueue)data);
    pthread_mutex_lock(&req_mutex);
    Request req;
    while(true){
        if(getRequestNumber(rq)>0){
            //printf("\033[0;32m[#] worker is getting request socket_d from queue\n\033[0m");
            req = getRequest(rq, &req_mutex);
            if(req){
                handleRequest(getClientSockd(req),getLogFD(req));
                free(req);
            }

        }else{
            pthread_cond_wait(&got_request, &req_mutex);
             //printf("\033[0;32m[#] worker signaled to Wake Up\n\033[0m");
        }
    }
}
int main(int argc, char** argv) {
    UINT8 Nflag = 0;
    // making the assumption that the number of input threads cannot be greater than 255
    UINT8 threadNum = 4;
    uint8_t logFlag = 0;
    char* logFile;
    char* portNumber = 0;
    UINT8 portFlag = 0;
    int c;

    /*
        input includes 2 optional arguments and 1 required:
            -l and -N for log file and thread number are the two optional reqs
            portNumber is the required argument

        ./httpserver -l logFile -N threadNum portNumber

        the below uses getopt to parse the command line options and port number!
    */
    opterr = 0;
    while((c = getopt(argc, argv, "-N:l:"))!= -1){
        switch(c){
            case 'N':
                if(optarg == 0){
                        fprintf(stderr, "%c flag must have number after it\n", optopt);
                        exit(1);
                    }
                Nflag = 1;
                threadNum = atoi(optarg);
                break;
            case 'l':
                if(optarg == 0){
                        fprintf(stderr, "%c flag must a valid logfile after it\n", optopt);
                        exit(1);
                    }
                logFlag = 1;
                logFile = optarg;
                break;
            case 1:
                //non optionary character code when we use - as the first char in optstring
                if(portFlag == 0){
                    portNumber = optarg;
                    portFlag = 1;
                }else{
                    fprintf(stderr, "%s is not a valid command line argument, try again\n", optarg);
                    exit(1);
                }
                break;
            case '?':
                if(optopt == 'N'){
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                }else if(optopt == 'l'){
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                }else{
                    fprintf(stderr, "erroneous option character \\x%x'\n", optopt);
                }
                exit(1);
            default:
                abort();
        }
    }
   if(argc < 2){
       //printf("ERROR: no arguments provided\n");
       exit(1);
   }
    if(!portNumber){
        //printf("ERROR: no port Number argument was provided\n");
       exit(1);
    }
    char* port = argv[1];
    //printf("[+] N flag is: %d\n", Nflag);
    //printf("[+] log flag is: %d\n", logFlag);
    //printf("[+] log file is: %s\n", logFile);
    //printf("[+] thread number is: %d\n", threadNum);
    //printf("[+] port number is: %s\n", portNumber);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(server_addr);
    /*
        Create server socket
    */
    int server_sockd = socket(AF_INET, SOCK_STREAM, 0);

    // Need to check if server_sockd < 0, meaning an error
    if (server_sockd < 0) {
        perror("socket");
    }
    /*
        Configure server socket
    */
    int enable = 1;

    /*
        This allows you to avoid: 'Bind: Address Already in Use' error
    */
    int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    /*
        Bind server address to socket that is open
    */
    ret = bind(server_sockd, (struct sockaddr *) &server_addr, addrlen);

    /*
        Listen for incoming connections
    */
    ret = listen(server_sockd, 5); // 5 should be enough, if not use SOMAXCONN

    if (ret < 0) {
        return EXIT_FAILURE;
    }
    /*
        Connecting with a client
    */
    struct sockaddr client_addr;
    socklen_t client_addrlen = sizeof(client_addr);
    
    int log_fd;
    if(logFlag) log_fd = open(logFile, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
    else log_fd = -1;
    RequestQueue request_queue = newRequestQueue();

    
    //initialize "worker" threads
    int threadID[threadNum];
    //dummy statements to get rid of warnings
    if(sizeof(threadID) == 0){

    }
    if(Nflag == 2){

    }
    pthread_t p_threads[threadNum];
    //create "worker" threads;
    for(UINT8 i = 0; i < threadNum; i++){
        threadID[i] = i;
        pthread_create(&p_threads[i], NULL, handleRequests,(void*)(request_queue));
    }

    

    while (true) {
        //printf("\033[0;32m[#]\n[+] server is waiting...\n\033[0m");
        int client_sockd = accept(server_sockd, &client_addr, &client_addrlen);
        pushRequest(request_queue, client_sockd, log_fd, &req_mutex, &got_request);
        printf("offset is %ld", offset);
    }
    free(&request_queue);
    if(log_fd != -1){
        close(log_fd);
    }
    return EXIT_SUCCESS;
}