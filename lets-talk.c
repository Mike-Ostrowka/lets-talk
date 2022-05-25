#include "list.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>


#define NUM_THREADS 4
#define NUM_LISTS 2

//constants
const char LOCAL_HOST[] = "127.0.0.1";
const char EXIT_CODE[] = "!exit";
const char STATUS_CODE[] = "!status";
const char REQ_STATUS[] = "!reqStatus";
const char ONLINE[] = "Online";
const char OFFLINE[] = "Offline";
const int ENCRYPTION_KEY = 21;

//globals
pthread_t threads[NUM_THREADS];
List* lists[NUM_LISTS];
char *remoteIP;
int localPort;
int remotePort;

//enum for thread number
enum threads {
    keyboard,
    receiver,
    printer,
    sender
};

//enum for list number
enum lists {
    send_list,
    receive_list
};

//globals for mutexes and exitting
bool exit_status = false;
int awaiting_status = 0;
pthread_mutex_t status_lock;

//subroutine for List_free
void freeString(void *item) {
    free(item);
    item = NULL;
}

//print usage when cannot connect to provided address
void usage() {
    printf("Usage:\n./lets-talk <local port> <remote host> <remote port>\n");
    printf("Examples:\n./lets-talk 3000 182.168.0.513 3001\n");
    printf("./lets-talk 3000 some-computer-name 3001\n");
    exit(1);
}

//cipher encrypt a string
char * encrypt(char *string) {
    for(int i = 0; i < strlen(string); i++) {
        unsigned char c = string[i];
        c = ((c + ENCRYPTION_KEY));
        string[i] = c;
    }
    return string;
}

//decrypt an encrypted string
char * decrypt(char *encrypted) {
    for(int i = 0; i < strlen(encrypted); i++) {
        unsigned char c = encrypted[i];
        c = ((c - ENCRYPTION_KEY));
        encrypted[i] = c;
    }
    return encrypted;
}

//thread for printing recieved data
void *printer_thread() {
    printf("Welcome to Lets-Talk! Please type your messages now.\n");
    
    while(1) { //main while loop
        int count;
        if((count = List_count(lists[receive_list])) > 0) { //check if data available
            while(List_count(lists[receive_list]) > 0) {
                
                //process data and print
                List_first(lists[receive_list]);
                void *pItem = List_first(lists[receive_list]);
                List_remove(lists[receive_list]); 
                char *message = pItem;

                printf("%s", message);

                if(strncmp(OFFLINE, message, 7) == 0) {
                    printf("\n");
                }

                if(strncmp(ONLINE, message, 6) == 0) {
                    printf("\n");
                }
                free(pItem);
            }
            fflush(stdout);  
        }
        usleep(100); //sleep while no work
    }
    pthread_exit(NULL);
}

//thread for capturing user input
void *keyboard_thread() {
    char buffer[4096];
    while(1) { //main while loop

        //get and process keyboard input
        fgets(buffer, 4096, stdin);
        char * newItem = (char *)malloc(strlen(buffer) + 1);
        strcpy(newItem, buffer);
        newItem[strlen(buffer)] = '\0';
        
        //check for status code
        if(strncmp(newItem, STATUS_CODE, 7) == 0) {
            pthread_mutex_lock(&status_lock);
            if(awaiting_status == 0) { 
                awaiting_status = 1;
            }
            pthread_mutex_unlock(&status_lock);
        }

        //encrypt and add to list for send thread
        encrypt(newItem);
        List_add(lists[send_list], newItem);
        while(awaiting_status == 1) { //sleep while waiting for status
            usleep(100);
        }
    }
    pthread_exit(NULL);
}

//thread for recieving messages
void *receiver_thread() {
    char buffer[4096];

    struct sockaddr_in clientInfo;
    struct sockaddr_in socketInfo;

    struct timeval time;
    time.tv_usec = 0;
    time.tv_sec = 1;
    
    //create a socket
    int socketStatus;
    if((socketStatus = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Failed to create a socket client");
        exit(1);
    }

    memset(&socketInfo, 0, sizeof(socketInfo));
    memset(&clientInfo, 0, sizeof(clientInfo));

    //set info
    socketInfo.sin_family = AF_INET;
    socketInfo.sin_port = htons(localPort);
    socketInfo.sin_addr.s_addr = INADDR_ANY;

    //bind to socket
    if(bind(socketStatus, (const struct sockaddr *)&socketInfo, sizeof(socketInfo)) < 0) {
        perror("failed to bind socket");
        exit(1);
    }

    //set a socket timeout
    int sockOptStatus = setsockopt(socketStatus, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof(time));
    if(sockOptStatus < 0) {
        perror("failed to set sock options");
        exit(1);
    }

    while(1) { //main loop

        //check status 
        pthread_mutex_lock(&status_lock);
        if(awaiting_status > 0) {
            awaiting_status++;
        }

        //set status offline if no response
        if(awaiting_status == 2) {
            char *item = (char *)malloc(strlen(OFFLINE) + 1);
            strcpy(item, OFFLINE);
            item[strlen(OFFLINE)] = '\0';
            awaiting_status = 0;
            List_add(lists[receive_list], item);
            pthread_mutex_unlock(&status_lock);
            continue;
        }
        pthread_mutex_unlock(&status_lock);

        //recieve packet or timeout
        socklen_t length = sizeof(clientInfo);
        int n = recvfrom(socketStatus, (char *)buffer, 4096, MSG_WAITALL, (struct sockaddr *)&clientInfo, &length);
        
        if(n > 1) { //if not timeout
            
            //decrypt buffer and check for !status or !exit
            buffer[n] = '\0';
            decrypt(buffer);

            //send exit to other thread
            if(strncmp(EXIT_CODE, buffer, 5) == 0) {
                char *item = (char *)malloc(strlen(EXIT_CODE) + 1);
                strcpy(item, EXIT_CODE);
                List_add(lists[send_list], item);
                continue;
            }

            //send a status response to other user
            if(strncmp(STATUS_CODE, buffer, 7) == 0) {
                char *item = (char *)malloc(strlen(REQ_STATUS) + 1);
                strcpy(item, REQ_STATUS);
                encrypt(item);
                List_add(lists[send_list], item);
                continue;
            }

            //confirmed other user is online
            if(strncmp(REQ_STATUS, buffer, 10) == 0) {
                pthread_mutex_lock(&status_lock);
                awaiting_status = 0;
                char *item = (char *)malloc(strlen(ONLINE) + 1);
                strcpy(item, ONLINE);
                item[strlen(ONLINE)] = '\0';
                List_add(lists[receive_list], item);
                pthread_mutex_unlock(&status_lock);
                continue;   
            }

            //save data to printer list
            char *item = (char *)malloc(strlen(buffer) + 1);
            strcpy(item, buffer);
            List_add(lists[receive_list], item);

        } 
    }
    close(socketStatus);
    pthread_exit(NULL);
}

//thread for sending messages
void *sender_thread() {
    struct sockaddr_in socketInfo;
    
    //connect to socket
    int socketStatus;
    if((socketStatus = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Failed to create a socket client");
        exit(1);
    }

    memset(&socketInfo, 0, sizeof(socketInfo));

    //set info
    socketInfo.sin_family = AF_INET;
    socketInfo.sin_port = htons(remotePort);
    struct in_addr ip;
    inet_aton(remoteIP, &ip);
    socketInfo.sin_addr.s_addr = (in_addr_t)ip.s_addr;

    while(1) { //main loop
        if(List_count(lists[send_list]) > 0) { //if data to send
            
            //get data from list
            List_first(lists[send_list]);
            void *pItem = List_first(lists[send_list]);
            List_remove(lists[send_list]); 
            char *message = pItem;
            
            //check for exit code
            if(strncmp(EXIT_CODE, message, 4) == 0) {
                exit_status = true;
                encrypt(message);
            }

            //send data to other user
            int sendStatus = sendto(socketStatus, (const char *)message, strlen(message), MSG_CONFIRM, (const struct sockaddr *) &socketInfo, sizeof(socketInfo));
            if(sendStatus < 0) {
                perror("Failed to send");
            }
            free(pItem);
            if(exit_status) {
                break;
            }
        }
        usleep(100); //sleep if no data
    }
    close(socketStatus);
    pthread_exit(NULL);
}

//check if remote port is available
void checkRemote(char *name, char *port) {
    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICSERV;
    // hints.ai_flags = AI_PASSIVE;

    int addrStatus;
    if((addrStatus = getaddrinfo(name, port, &hints, &servinfo)) != 0) {
        freeaddrinfo(servinfo);
        usage();
    }
    freeaddrinfo(servinfo);
}

//check if local port is available
void checkLocal(char *port) {
    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICSERV;
    // hints.ai_flags = AI_PASSIVE;

    int addrStatus;
    if((addrStatus = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        freeaddrinfo(servinfo);
        usage();
    }
    freeaddrinfo(servinfo);
}

int main(int argc, char *argv[]) {

    if(argc != 4) {
        usage();
    }

    //convert localhost to numerical representation
    if(strcmp(argv[2], "localhost") == 0) {
        remoteIP = (char *)malloc(strlen(LOCAL_HOST) + 1);
        strcpy(remoteIP, LOCAL_HOST);
    } else {
        remoteIP = (char *)malloc(strlen(argv[2]) + 1);
        strcpy(remoteIP, argv[2]);    
    }

    remotePort = atoi(argv[3]);
    localPort = atoi(argv[1]);

    //check server status
    checkLocal(argv[1]);
    checkRemote(remoteIP, argv[3]);

    //create lists
    lists[send_list] = List_create();
    lists[receive_list] = List_create();

    //create threads
    int threadStatus;
    threadStatus = pthread_create(&threads[keyboard], NULL, keyboard_thread, NULL);
    if(threadStatus) {
        perror("Failed to create keyboard thread");
        exit(1);
    }

    threadStatus = pthread_create(&threads[receiver], NULL, receiver_thread, NULL);
    if(threadStatus) {
        perror("Failed to create reciever thread");
        exit(1);
    }

    threadStatus = pthread_create(&threads[printer], NULL, printer_thread, NULL);
    if(threadStatus) {
        perror("Failed to create printer thread");
        exit(1);
    }
    threadStatus = pthread_create(&threads[sender], NULL, sender_thread, NULL);
    if(threadStatus) {
        perror("Failed to create sender thread");
        exit(1);
    }

    //sleep until exit
    while(!exit_status) {
        sleep(1);
    }

    //cancel all threads
    for(int i = 0; i < 4; i++) {
        pthread_cancel(threads[i]);
    }

    //free lists
    free(remoteIP);
    List_free(lists[send_list], freeString);
    List_free(lists[receive_list], freeString);
    pthread_exit(NULL);
    return 0;
}

