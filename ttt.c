#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/select.h>
#include <pthread.h>

#define BUFFERSIZE 1024

//Changes if user signals with ctrl-c, or if server closes (read returns 0 on server output)
int volatile active = 1;
int sock;
char buffer[BUFFERSIZE];

//Sigint handler
void signal_handler(int sig_num) {
    printf("Closing program!\n");
    exit(EXIT_FAILURE);
}

// Makes a connection to the server and returns the socket
int connect_server(char* server_address, char* port) {
    struct addrinfo hints, *info_list, *info;
    int sock, err;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    err = getaddrinfo(server_address, port, &hints, &info_list);
    if(err) {
        printf("Could not obtain address information of the provided domain / port.\n");
        // Error in obtaining address information of the given domain and port
        return -1;
    }
    for(info = info_list; info != NULL; info = info->ai_next) {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if(sock < 0) {
            continue;
            // go to next iteration if socket fails
        }
        err = connect(sock, info->ai_addr, info->ai_addrlen);
        if(err) {
            close(sock);
            continue;
            // go to next iteration if connection fails
        }
        // socket and conneciton successful, break.
        break;
    }
    freeaddrinfo(info_list);
    // traversed through linked list, no connection made
    if(info == NULL) {
        printf("Unable to establish any connections.\n");
        return -1;
    }
    // traversed through linked list, made a connection
    return sock;
}

void parse_buffer(char* buffer, int bytes) {
    char line[BUFFERSIZE];
    int line_cntr = 0, i;
    for(i = 0; i < bytes; i++) {
        if(buffer[i] != '\0') {
            line[line_cntr] = buffer[i];
            line_cntr++;
        }
        else {
            line[line_cntr] = buffer[i];
            printf("%s", line);
            fflush(stdout);
            // If the line is a BEGN command, read and print an empty tic tac toe board
            if(line[0] == 'B' && line[1] == 'E' && line[2] == 'G' && line[3] == 'N') {
                for(int x = 0; x < 3; x++) {
                    printf("|.|.|.|\n");
                }
            }
            // If the line is a MOVD command, read and print out a 3x3 tic tac toe board
            if(line[0] == 'M' && line[1] == 'O' && line[2] == 'V' && line[3] == 'D') {
                //MOVD|17|X|2,2|.........| -> startat index 10 to start reading the board
                for(int x = 14; x < 23; x += 3) {
                    printf("|%c|%c|%c|\n", line[x], line[x+1], line[x+2]);
                }
            }
            line_cntr = 0;
        }
    }
}

void* user_input(void* args) {
    printf("Connected to server. Please wait for an opponent to connect.\n");
    int bytes;
    while(active) {
        bytes = read(STDIN_FILENO, buffer, BUFFERSIZE);
        if(bytes < 0) {
            printf("Error: Failed to read().\n");
            active = 0;
            break;
        }
        buffer[bytes] = '\0';
        write(sock, buffer, bytes+1);
    }
    pthread_exit(NULL);
}

void* server_output(void* args) {
    pthread_t user_thread = *(pthread_t*)args;
    while(active) {
        int bytes;
        bytes = read(sock, buffer, BUFFERSIZE);
        if(bytes < 0) {
            printf("Error: Failed to read().\n");
            active = 0;
            pthread_cancel(user_thread);
            break;
        }
        if(bytes == 0) {
            printf("Connection Terminated by Server.\n");
            active = 0;
            pthread_cancel(user_thread);
            break;
        }
        if(strcmp(buffer, "Client 1 has terminated.\n") == 0 || strcmp(buffer, "Client 2 has terminated.\n") == 0) {
            parse_buffer(buffer, bytes);
            active = 0;
            pthread_cancel(user_thread);
            break;
        }
        if (strcmp(buffer, "ping") == 0){
            //  Checking if connection is still alive
            write(sock, "ping", 5);
            continue;
        }
        parse_buffer(buffer, bytes);
    }
    pthread_exit(NULL);
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);

    char* server_address = (argc > 1) ? argv[1] : "localhost";
    char* port = (argc > 2) ? argv[2] : "21888";
    sock = connect_server(server_address, port);

    // connect_server failed, exit program
    if(sock < 0) {
        printf("Error in connecting to the server.\n");
        return -1;
    }

    pthread_t user_thread, server_thread, err;

    err = pthread_create(&user_thread, NULL, user_input, NULL);
    if(err != 0) {
        printf("Failed to create user input thread.\n");
        return -1;
    }
    err = pthread_create(&server_thread, NULL, server_output, &user_thread);
    if(err != 0) {
        printf("Failed to create server output thread.\n");
        return -1;
    }
    err = pthread_join(user_thread, NULL);
    if(err != 0) {
        printf("Failed to join user input thread.\n");
        return -1;
    }
    err = pthread_join(server_thread, NULL);
    if (err != 0) {
        printf("Failed to join server output thread.\n");
        return -1;
    }
    // For the threads to end, active must equal 0.  Either user hit ctrl-c or server terminated / read() error.
    if(active == 0) {
        close(sock);
        return 0;
    }
    
    
}