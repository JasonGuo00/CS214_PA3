#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <signal.h>
#include <sys/select.h>
#include <time.h>

#define BUFFERSIZE 1024
#define BOARDSIZE 10

typedef struct player{
    char* name;
    int name_len;
    char role;
}player;
typedef struct game{
    player* player1;
    player* player2;
    int clients[2];
    struct sockaddr_storage* strg_list[2];
    socklen_t len_list[2];
    char board[BOARDSIZE];
    // 0 = player 1's turn, 1 = player 2's turn
    int curr_move;
    int game_status;
    int draw_requester;
    pthread_mutex_t lock;
} game;
typedef struct thread_arguments{
    int sock1;
    int sock2;
    pthread_t* thread1;
    pthread_t* thread2;
    char client_name[100];
    char client_port[50];
    int p_len;
    game* game_object;
}t_args;

int volatile active = 1;

typedef struct username_node{
    char* username;
    struct username_node* next;
} username;

username* username_head = NULL;
username* username_tail = NULL;

void remove_username(char* username_to_remove){
    username* prev = NULL;
    username* username_ptr;
    for (username_ptr = username_head; username_ptr != NULL; username_ptr = username_ptr->next){
        if (strcmp(username_ptr->username, username_to_remove) == 0){
            if (prev != NULL){
                prev->next = username_ptr->next;
            }
            else{ //    Is head
                username_head = username_ptr->next;
            }
            if (username_tail == username_ptr){
                username_tail = NULL;
            }
            free(username_ptr);
            break;
        }
        prev = username_ptr;
    }
}

void add_username(char* username_to_add){
    username* new_username = malloc(sizeof(username));
    new_username->next = NULL;
    new_username->username = username_to_add;
    if (username_tail == NULL){
        username_head = new_username;
        username_tail = new_username;
    }
    else{
        username_tail->next = new_username;
        username_tail = new_username;
    }
}

int username_exists(char* username_to_check){
    for (username* username_ptr = username_head; username_ptr != NULL; username_ptr = username_ptr->next){
        if (strcmp(username_ptr->username, username_to_check) == 0){
            return 1;
        }
    }
    return 0;
}

void free_un_list(username* node){
    if (node == NULL){
        return;
    }
    if (node->next != NULL){
        free_un_list(node->next);
    }
    free(node);
}

pthread_mutex_t username_list_lock;

void resigned(player* pl,int sock1,int sock2, pthread_t thread1, pthread_t thread2, game* game_object) {
    player* player1 = game_object->player1;
    player* player2 = game_object->player2;
    if(strcmp(pl->name, player1->name) == 0) {
        char message[100];
        strcpy(message,"OVER|20|L|You have resigned|");
        write(sock1, message, 29);
        write(sock1, "\n", 2);
        sprintf(message, "OVER|%d|W|%s has resigned|", player1->name_len +16,player1->name);
        // OVER|%d| -> 8 bits + player1->name_len+16 + 1(null terminator) = total bits written
        write(sock2, message, player1->name_len + 25);
        write(sock2, "\n", 2);
    }
    else {
        char message[100];
        strcpy(message,"OVER|20|L|You have resigned|");
        write(sock2, message, 29);
        write(sock2, "\n", 2);
        sprintf(message, "OVER|%d|W|%s has resigned|", player2->name_len +16,player2->name);
        // OVER|%d| -> 8 bits + player2->name_len+16 + 1(null terminator) = total bits written
        write(sock1, message, player2->name_len + 25);
        write(sock1, "\n", 2);
    }
    pthread_cancel(thread1);
    pthread_cancel(thread2);
}
int countbytes(char* string) {
    int i = 0;
    while(string[i] != '\0') {
        i++;
    }
    // return the number of characters including null terminator
    return ++i;
}
int gameover(game* game_object) {
    player* player1 = game_object->player1;
    char board[BOARDSIZE];
    strcpy(board, game_object->board);
    // Check the rows
    for(int i = 0; i <= 6; i+=3) {
        if(board[i] != '.' && board[i] == board[i+1] && board[i] == board[i+2]) {
            if(board[i] == player1->role) {return 1;}
            else {return 2;}
        }
    }
    // Check the columns
    for(int i = 0; i < 3; i++) {
        if(board[i] != '.' && board[i] == board[i+3] && board[i] == board[i+6]) {
            if(board[i] == player1->role) {return 1;}
            else {return 2;}
        }
    }
    // Left->Right diagonal
    if(board[0] != '.' && board[0] == board[4] && board[0] == board[8]) {
        if(board[0] == player1->role) {return 1;}
        else {return 2;}
    }
    if(board[2] != '.' && board[2] == board[4] && board[2] == board[6]) {
        if(board[2] == player1->role) {return 1;}
        else {return 2;}
    }
    // Check for draw
    if (game_object->draw_requester == 2){
        return 3;
    }
    int draw_flag = 1;
    for(int i = 0; i < 9; i++) {
        if(board[i] == '.') {
            draw_flag = 0;
            break;
        }
    }
    if(draw_flag) {
        return 3;
    }
    else {
        return 0;
    }
}
void end_game(int winner,int sock1,int sock2, pthread_t thread1, pthread_t thread2, game* game_object) {
    player* player1 = game_object->player1;
    player* player2 = game_object->player2;
    printf("ENDING GAME...\n");
    if(winner == 1) {
        char message[100];
        strcpy(message,"OVER|16|W|You have won!|\n");
        write(sock1, message, 26);
        sprintf(message, "OVER|%d|L|%s has won!|\n", player1->name_len +12,player1->name);
        int b = countbytes(message);
        write(sock2, message, b+1);
    }
    else if(winner == 2) {
        char message[100];
        strcpy(message,"OVER|16|W|You have won!\n|");
        write(sock2, message, 26);
        sprintf(message, "OVER|%d|L|%s has won!\n|", player2->name_len +12,player2->name);
        int b = countbytes(message);
        write(sock1, message, b+1);
    }
    else {
        // DRAW
        char message[100];
        strcpy(message,"OVER|18|D|You have drawn|\n");
        write(sock2, message, 27);
        write(sock1, message, 27);
    }
    pthread_cancel(thread1);
    pthread_cancel(thread2);
}
int read_move(char role, int x, int y, game* game_object) {
    // Out of bounds check
    if(x > 3 || x <  1|| y > 3 || y < 1) {
        return -1;
    }
    // start_index = 0, 3, or 6.  Offset = 0, 1, or 2.
    // 0+offset covers first row of the board, 3+offset covers second row, 6+offset covers third row
    int start_index, offset = y-1;
    if(x == 1) {
        start_index = 0;
    }
    else if(x == 2) {
        start_index = 3;
    }
    else {
        start_index = 6;
    }
    if(game_object->board[start_index + offset] == '.') {
        game_object->board[start_index + offset] = role;
        return 1; 
    }
    else {
        // the space is already taken;
        return -2;
    }
    
}
void assign_role(game* game_object) {
    player* player1 = game_object->player1;
    player* player2 = game_object->player2;

    srand(time(NULL));
    int random = rand() & 1;

    if(random == 0) {
        player1->role = 'X';
        player2->role = 'O';
        game_object->curr_move = 0;
    }
    else {
        player1->role = 'O';
        player2->role = 'X';
        game_object->curr_move = 1;
    }
}
int len(char*);
int tokenizer(char* message, int bytes, char (**token_list)[bytes], int* n_tokens) {
    // Minimum bytes is 9-> CODE|0| + new line char + null terminator
    if(bytes < 9) {
        printf("bytes less than 9\n");
        return -1;
    }
    // Find out how many tokens there will be
    int numtokens = 0, iterate = 0;
    for(int i = 0; message[i] != '\0'; i++) {
        if(message[i] == '|') {
            numtokens++;
        }
    }
    // Minimum token size is 2: CODE|0|
    if(numtokens < 2) {
        printf("num tokens less than 2\n");
        return -1;
    }
    // If last character of message isn't a |, breach of protocol
    if(message[bytes-3] != '|') {
        printf("last char isn't |\n");
        return -1;
    }

    *n_tokens = numtokens;
    *token_list = malloc(sizeof(char[bytes]) * numtokens);
    for(int x = 0; x < numtokens; x++) {
        int cnt = 0, i;
        for(i = iterate; message[i] != '|' && message[i] != '\0'; i++) {
            (*token_list)[x][cnt] = message[i];
            cnt++;
        }
        iterate = ++i;
        (*token_list)[x][cnt] = '\0';
    }
    //  Check that token 2 is a number
    if(strcmp((*token_list)[1], "0") != 0 && atoi((*token_list)[1]) == 0) {
        printf("token 2 isn't a number\n");
        printf("%s, %d\n", (*token_list)[1], atoi((*token_list)[1]));
        return -1;
    }
    //  Check that length is correct
    if(atoi((*token_list)[1]) < 10) {
        if (atoi((*token_list)[1]) != len(message)-8){
            printf("message length incorrect\n");
            return -1;
        }
    }
    else if(atoi((*token_list)[1]) < 100) {
        if (atoi((*token_list)[1]) != len(message)-9){
            printf("message length incorrect\n");
            return -1;
        }
    }
    else {
        if (atoi((*token_list)[1]) != len(message)-10){
            printf("message length incorrect\n");
            return -1;
        }
    }
    
    // Check that message is not over 255
    if((atoi((*token_list)[1])) > 255 || (atoi((*token_list)[1]) < 0)) {
        printf("message length is too long or too short\n");
        return -1;
    }
    return 1; 
}
int initialize(char* port_num) {
    int queue_size = 8;
    int sock, err;

    struct addrinfo hints, *info, *info_list;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    err = getaddrinfo(NULL, port_num, &hints, &info_list);
    if(err) {
        printf("Error: getaddrinfo failed.\n");
        return -1;
    }
    // attempt to create socket here
    for(info = info_list; info != NULL; info = info->ai_next) {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if(sock < 0) {
            // fail to make socket, continue.
            printf("Error: Failed to create socket.\n");
            continue;
        }
        err = bind(sock, info->ai_addr, info->ai_addrlen);
        if(err) {
            // fail to bind, close socket and continue.
            printf("Error: Failed to bind.\n");
            close(sock);
            continue;
        }
        err = listen(sock,queue_size);
        if(err) {
            // fail to set up listener, close socket and continue.
            printf("Error: Failed to create listener.\n");
            close(sock);
            continue;
        }
        // Success or reached the end of info_list
        break;
    }
    freeaddrinfo(info_list);

    if(info == NULL) {
        // Failed to create socket.
        printf("Error: Failed to create socket.\n");
        return -1;
    }
    return sock;
}

int len(char* str){
    int length = 0;
    for (int i = 0; str[i] != 0; i++){
        length++;
    }
    return length;
}

//wait_play sock1 handler
void* wp_sock1_handler(void* arg) {
    t_args* arguments = (t_args*) arg;
    int num_tokens1, err;
    int sock1 = arguments->sock1, sock2 = arguments->sock2;
    int bytes;
    game* game_object = arguments->game_object;
    player* player1 = game_object->player1;

    char buffer[BUFFERSIZE];
    while(active) {
        bytes = read(sock1, buffer, BUFFERSIZE);
        if(bytes == 0) {
            write(sock2, "Client 1 has terminated.\n", 26);
            printf("Client 1 has terminated.\n");
            break;
        }
        pthread_mutex_lock(&game_object->lock);
        char (*token_list1)[bytes];
        err = tokenizer(buffer, bytes, &token_list1, &num_tokens1);
        if(err != -1 && strcmp(token_list1[0], "PLAY") == 0 && num_tokens1 == 3) {
            //  Make sure name isn't taken
            pthread_mutex_lock(&username_list_lock);

            if (!username_exists(token_list1[2])){
                int username_length = len(token_list1[2]);
                player1->name = malloc(username_length+1);
                player1->name_len = username_length;

                strcpy(player1->name, token_list1[2]);
                add_username(player1->name);
                pthread_mutex_unlock(&username_list_lock);
                arguments->p_len = atoi(token_list1[1]);
                free(token_list1);
                write(sock1, "WAIT|0|\n",9);
                pthread_mutex_unlock(&game_object->lock);
                break;
            }
            else{
                write(sock1, "INVL|26|Username is not available|\n", 36);
                free(token_list1);
            }

            pthread_mutex_unlock(&username_list_lock);
        }
        else {
            write(sock1, "INVL|24|Your request is invalid|\n", 34);
        }
        pthread_mutex_unlock(&game_object->lock);
    }
    pthread_exit(NULL);
}
//wait_play sock2 handler
void* wp_sock2_handler(void* arg) {
    t_args* arguments = (t_args*) arg;
    int num_tokens2, err;
    int sock1 = arguments->sock1, sock2 = arguments->sock2;
    int bytes;
    game* game_object = arguments->game_object;
    player* player2 = game_object->player2;

    char buffer[BUFFERSIZE];
    while (active) {
        bytes = read(sock2, buffer, BUFFERSIZE);
        if(bytes == 0) {
            write(sock1, "Client 2 has terminated.\n", 26);
            printf("Client 2 has terminated.\n");
            break;
        }
        pthread_mutex_lock(&game_object->lock);
        char (*token_list2)[bytes];
        err = tokenizer(buffer, bytes, &token_list2, &num_tokens2);
        if(err != -1 && strcmp(token_list2[0], "PLAY") == 0 && num_tokens2 == 3) {
           //  Make sure name isn't taken
            pthread_mutex_lock(&username_list_lock);

            if (!username_exists(token_list2[2])){
                int username_length = len(token_list2[2]);
                player2->name = malloc(username_length+1);
                player2->name_len = username_length;

                strcpy(player2->name, token_list2[2]);
                add_username(player2->name);
                pthread_mutex_unlock(&username_list_lock);
                arguments->p_len = atoi(token_list2[1]);
                free(token_list2);
                write(sock2, "WAIT|0|\n",9);
                pthread_mutex_unlock(&game_object->lock);
                break;
            }
            else{
                write(sock2, "INVL|26|Username is not available|\n", 36);
                free(token_list2);
            }

            pthread_mutex_unlock(&username_list_lock);
        }
        else {
            write(sock2, "INVL|24|Your request is invalid|\n", 34);
        }
        pthread_mutex_unlock(&game_object->lock);
    }
    pthread_exit(NULL);
}
// Waits for both players to use the PLAY message to begin the game
int wait_play(game* game_object) {
    // set player names NULL for checking purposes
    game_object->player1->name = NULL;
    game_object->player2->name = NULL;

    int sock1 = game_object->clients[0];
    int sock2 = game_object->clients[1];

    assign_role(game_object);
    write(sock1, "Connected to opponent.  Please indicate readiness with the PLAY command!\n", 74);
    write(sock2, "Connected to opponent.  Please indicate readiness with the PLAY command!\n", 74);

    t_args* args1;
    args1 = malloc(sizeof(t_args));
    args1->sock1 = sock1;
    args1->sock2 = sock2;
    args1->p_len = 0;
    args1->game_object = game_object;

    t_args* args2;
    args2 = malloc(sizeof(t_args));
    args2->sock1 = sock1;
    args2->sock2 = sock2;
    args2->p_len = 0;
    args2->game_object = game_object;

    pthread_t p1_thread, p2_thread;

    if(pthread_create(&p1_thread, NULL, wp_sock1_handler, args1) != 0) {
        printf("Failed to create player1 thread.\n");
        exit(EXIT_FAILURE);
    }
    if(pthread_create(&p2_thread, NULL, wp_sock2_handler, args2) != 0) {
        printf("Failed to create player2 thread.\n");
        exit(EXIT_FAILURE);
    }
    if(pthread_join(p1_thread, NULL) != 0) {
        printf("Failed to join player1 thread.\n");
        exit(EXIT_FAILURE);
    }
    if(pthread_join(p2_thread, NULL) != 0) {
        printf("Failed to join player2 thread.\n");
        exit(EXIT_FAILURE);
    }

    if(args1->p_len && args2->p_len) {
        int p1_len = args1->p_len;
        int p2_len = args2->p_len;
        player* player1 = game_object->player1;
        player* player2 = game_object->player2;
        player1->name_len = p1_len-1;
        player2->name_len = p2_len-1;
        char temp1[13+p2_len];
        char temp2[13+p1_len];
        sprintf(temp1, "BEGN|%d|%c|%s|\n", p2_len+2, player1->role, player2->name);
        sprintf(temp2, "BEGN|%d|%c|%s|\n", p1_len+2, player2->role, player1->name);
        int offset1, offset2;
        offset1 = p2_len >= 8 ? (p2_len >= 98 ? 11 : 10) : 9;
        offset2 = p1_len >= 8 ? (p1_len >= 98 ? 11 : 10) : 9;
        write(sock1, temp1, offset1 + p2_len+2);
        write(sock2, temp2, offset2 + p1_len+2);
        game_object->game_status = 1;
        free(args1);
        free(args2);
        return 1;
    }
    free(args1);
    free(args2);
    return -1;
}
// Interprets player commands after game starts
char* interpreter(player* pl, int bytes, char(**tokenlist)[bytes], int numtokens, game* game_object) {
    char* return_str = malloc(sizeof(char)*300);
    // Check if it is the requesting player's turn
    int isTurn = 0;
    char role = pl->role;
    if(strcmp(pl->name, game_object->player1->name) == 0 && game_object->curr_move == 0) {
        isTurn = 1;
    } 
    else if( strcmp(pl->name, game_object->player2->name) == 0 && game_object->curr_move == 1) {
        isTurn = 1;
        role = pl->role;
    }
    else {
        role = pl->role;
    }
    // Read and interpret the message code
    char* code = (*tokenlist)[0];
    if(game_object->game_status == 1 && strcmp(code, "PLAY") == 0) {
        if (game_object->draw_requester != -1){
            strcpy(return_str,"INVL|45|Error: outstanding draw suggestion|\n");
            return return_str;
        }
        // Player attempts to start game when already began
        strcpy(return_str, "INVL|35|PLAY error: Game has already begun|\n");
        return return_str;
    }
    else if(game_object->game_status == 1 && strcmp(code, "MOVE") == 0) {
        if (game_object->draw_requester != -1){
            strcpy(return_str,"INVL|45|Error: outstanding draw suggestion|\n");
            return return_str;
        }
        // Player would like to move -> check if it is their turn
        if(isTurn == 0) {
            strcpy(return_str,"INVL|34|MOVE error: Not your turn to move|\n");
            return return_str;
        }
        // Check that there are a correct number of tokens
        if(numtokens != 4) {
            strcpy(return_str,"INVL|30|MOVE error: 4 tokens expected|\n");
            return return_str;
        }
        // MOVE|num|role|x,y| -> need to check if role is actually the player's
        if((*tokenlist)[2][0] != role) {
            strcpy(return_str,"INVL|27|MOVE error: Incorrect role|\n");
            return return_str;
        }
        else if((*tokenlist)[2][1] != '\0') {
            strcpy(return_str,"INVL|61|MOVE error: Please indicate role with a single letter X or O.\n");
            return return_str;
        }
        // Need to check if token 4 is in the order of (x,y), with x and y being integers
        for(int z = 0; (*tokenlist)[3][z] != '\0'; z++) {
            if(z == 0 && !((*tokenlist)[3][z] >= '0' && (*tokenlist)[3][z] <= '9')) {
                strcpy(return_str,"INVL|83|MOVE error: Token 4 is invalid. Please indicate move with position in the form x,y|\n");
                return return_str;
            }
            else if(z == 1 && !((*tokenlist)[3][z] == ',')) {
                strcpy(return_str,"INVL|83|MOVE error: Token 4 is invalid. Please indicate move with position in the form x,y|\n");
                return return_str;
            }
            else if(z == 2 && !((*tokenlist)[3][z] >= '0' && (*tokenlist)[3][z] <= '9')) {
                strcpy(return_str,"INVL|83|MOVE error: Token 4 is invalid. Please indicate move with position in the form x,y|\n");
                return return_str;
            }
            else if(z > 2) {
                strcpy(return_str,"INVL|83|MOVE error: Token 4 is invalid. Please indicate move with position in the form x,y|\n");
                return return_str;
            }
            // Loop terminates without returning, means that token 4 is acceptable
        }
        // Code is matched, token 2 is a number, token 3 matches player's role, token 4 format is valid, attempt to perform move.
        int result = read_move(role, (*tokenlist)[3][0] - '0',(*tokenlist)[3][2] - '0', game_object);
        if(result == 1) {
            // Success
            // 8 + 1 + 3 + 9 + 3 = 24 characters + 1 null terminator = 25 char -> MOVD|17|X|1,1|X........|\0
            game_object->curr_move = !game_object->curr_move;
            sprintf(return_str,"MOVD|17|%c|%s|%s|\n", role, (*tokenlist)[3], game_object->board);
            pthread_mutex_unlock(&game_object->lock);
            return return_str;
        }
        else {
            // Failed to read move
            if(result == -1) {
                strcpy(return_str,"INVL|65|MOVE error: Make sure that x and y are between 1 and 3 inclusive|\n");
                return return_str;
            }
            else {
                strcpy(return_str,"INVL|35|MOVE error: That space is occupied|\n");
                return return_str;
            }
        }
    }
    // Player would like to resign -> can resign if not their turn
    else if(game_object->game_status == 1 && strcmp(code, "RSGN") == 0) {
        if (game_object->draw_requester != -1){
            strcpy(return_str,"INVL|45|Error: outstanding draw suggestion|\n");
            return return_str;
        }
        if(numtokens != 2) {
            strcpy(return_str,"INVL|30|RSGN error: 2 tokens expected|\n");
            return return_str;
        }
        strcpy(return_str,"RSGN");
        return return_str;
        // Write to oppponent that player has resigned, write to both players OVER|0|, then end match
    }
    // Player would like to request a draw -> can request draw if not their turn
    else if(game_object->game_status == 1 && strcmp(code, "DRAW") == 0) {
        if(numtokens != 3) {
            strcpy(return_str,"INVL|30|DRAW error: 3 tokens expected|\n");
            return return_str;
        }
        int player_id = (pl == game_object->player2);
        if ((*tokenlist)[2][0] == 'S'){
            //  Suggesting a draw
            if (game_object->draw_requester != -1){
                strcpy(return_str,"INVL|35|DRAW error: draw already suggested|\n");
                return return_str;
            }
            
            game_object->draw_requester = player_id;

            int opposing_player_id = (player_id == 1) ? 0 : 1;
            write(game_object->clients[opposing_player_id], "DRAW|2|S|\n", 11);

            strcpy(return_str, "");
            return return_str;
        }
        else if (player_id != game_object->draw_requester){
            if ((*tokenlist)[2][0] == 'A'){
                //  Accepting a draw
                game_object->draw_requester = 2;
                strcpy(return_str,"OVER|2|D|\n");
                return return_str;
            }
            else if ((*tokenlist)[2][0] == 'R'){
                //  Rejecting a draw
                write(game_object->clients[game_object->draw_requester], "DRAW|2|R|\n", 11);
                game_object->draw_requester = -1;

                strcpy(return_str, "");
                return return_str;
            }
            else{
                strcpy(return_str,"INVL|33|DRAW error: invalid draw message|\n");
                return return_str;
            }
        }
        else{
            strcpy(return_str,"INVL|33|DRAW error: invalid draw message|\n");
            return return_str;
        }
        // Need to know if a request is already out, who requested the draw, who is expected to accept / decline.
        // Requestee can accept or deny draw.  Accepting ends game, declining continues.
    }
    strcpy(return_str,"TEMPORARY\n");
    return return_str;
}
void* socket1_handler(void* arg) {
    t_args* arguments = (t_args*) arg;
    int sock1 = arguments->sock1, sock2 = arguments->sock2;
    char client1_addr[100], client1_port[50];
    game* game_object = arguments->game_object;
    player* player1 = game_object->player1;
    strcpy(client1_addr, arguments->client_name);
    strcpy(client1_port, arguments->client_port);
    int bytes;
    char buffer[BUFFERSIZE];
    while(active) {
        bytes = read(sock1, buffer, BUFFERSIZE);
        pthread_mutex_lock(&game_object->lock);
        if(bytes == 0) {
            printf("[%s:%s] : Terminating connection.\n", client1_addr, client1_port);
            write(sock2, "Client 1 has terminated.\n", 26);
            pthread_mutex_unlock(&game_object->lock);
            break;
        }
        // 1. Tokenize the input -> use tokenizer()
        char (*token_list1)[bytes];
        int numtokens1, err2;
        err2 = tokenizer(buffer, bytes, &token_list1, &numtokens1);
        char* response;
        if(err2 == -1) {
            write(sock1, "INVL|38|Your request does not follow protocol|\n",48);
            response = NULL;
        }
        else {
            // 2. Interpret the input -> read the message, perform an action, return the server response string
            response = interpreter(player1, bytes, &token_list1, numtokens1, game_object); 
        }
        // 3. Send a response -> write the server response to the correct sockets
        if(response != NULL && strcmp(response, "RSGN") == 0) {
            resigned(player1, sock1, sock2, *(arguments->thread1), *(arguments->thread2), game_object);
            free(response);
            pthread_mutex_unlock(&game_object->lock);
            break;
        }
        //Figure out if response is sent to both players or only 1 -> MOVD(send to both), INVL(caller only),
        if(response != NULL && response[0] == 'I') {
            int b = countbytes(response);
            write(sock1, response, b);
        }
        if(response != NULL && response[0] == 'M') {
            int b = countbytes(response);
            write(sock1, response, b);
            write(sock2, response, b);
        }
        if(response != NULL) {
            
            int result = gameover(game_object);
            if(result) {
                end_game(result, sock1, sock2, *(arguments->thread1), *(arguments->thread2), game_object);
                free(response);
                pthread_mutex_unlock(&game_object->lock);
                break;
            }
        }
        pthread_mutex_unlock(&game_object->lock);
    }
    pthread_exit(NULL);
}
void* socket2_handler(void* arg) {
    t_args* arguments = (t_args*) arg;
    int sock1 = arguments->sock1, sock2 = arguments->sock2;
    char client2_addr[100], client2_port[50];
    game* game_object = arguments->game_object;
    player* player2 = game_object->player2;
    strcpy(client2_addr, arguments->client_name);
    strcpy(client2_port, arguments->client_port);
    int bytes;
    char buffer[BUFFERSIZE];
    while(active) {
        bytes = read(sock2, buffer, BUFFERSIZE);
        pthread_mutex_lock(&game_object->lock);
        if(bytes == 0) {
            printf("[%s:%s] : Terminating connection.\n", client2_addr, client2_port);
            write(sock1, "Client 2 has terminated.\n", 26);
            pthread_mutex_unlock(&game_object->lock);
            break;
        }
        // 1. Tokenize the input -> use tokenizer()
        char (*token_list2)[bytes];
        int numtokens2, err2;
        
        err2 = tokenizer(buffer, bytes, &token_list2, &numtokens2);
        char* response;
        if(err2 == -1) {
            write(sock2, "INVL|38|Your request does not follow protocol|\n",48);
            response = NULL;
        }
        else {
            response = interpreter(player2, bytes, &token_list2, numtokens2, game_object);
        }
        // 2. Interpret the input -> read the message, perform an action, return the server response string
        // 3. Send a response -> write the server response to the correct sockets
        if(response != NULL && strcmp(response, "RSGN") == 0) {
            resigned(player2, sock1, sock2, *(arguments->thread1), *(arguments->thread2), game_object);
            free(response);
            pthread_mutex_unlock(&game_object->lock);
            break;
        }
        //Figure out if response is sent to both players or only 1 -> MOVD(send to both), INVL(caller only),
        if(response != NULL && response[0] == 'I') {
            int b = countbytes(response);
            write(sock2, response, b);
        }
        if(response != NULL && response[0] == 'M') {
            int b = countbytes(response);
            write(sock1, response, b);
            write(sock2, response, b);
        }
        if(response != NULL){
            int result = gameover(game_object);
            if(result) {
                end_game(result, sock1, sock2, *(arguments->thread1), *(arguments->thread2), game_object);
                free(response);
                pthread_mutex_unlock(&game_object->lock);
                break;
            }
        }
        pthread_mutex_unlock(&game_object->lock);
    }
    pthread_exit(NULL);
}
// Reads messages sent by clients
void respond2(game* game_object) {
    int sock1 = game_object->clients[0];
    int sock2 = game_object->clients[1];
    struct sockaddr* client1 = (struct sockaddr* )game_object->strg_list[0];
    struct sockaddr* client2 = (struct sockaddr* )game_object->strg_list[1];
    socklen_t client1_length = game_object->len_list[0];
    socklen_t client2_length = game_object->len_list[1];
    player* player1 = game_object->player1;
    player* player2 = game_object->player2;

    char client1_addr[100], client1_port[50], client2_addr[100], client2_port[50];
    int err;

    err = getnameinfo(client1, client1_length, client1_addr, 100, client1_port, 50, NI_NUMERICSERV);
    if(err) {
        printf("Unable to obtain client information.\n");
        strcpy(client1_addr, "Connection1");
        strcpy(client1_port, "Connection2");
    }
    err = getnameinfo(client2, client2_length, client2_addr, 100, client2_port, 50, NI_NUMERICSERV);
    if(err) {
        printf("Unable to obtain client information.\n");
        strcpy(client2_addr, "Connection1");
        strcpy(client2_port, "Connection2");
    }

    printf("Connection between [%s:%s] and [%s:%s]\n", client1_addr, client1_port, client2_addr, client2_port);
    printf("%s (%c) VS. %s (%c)\n", player1->name, player1->role, player2->name, player2->role);

    t_args* args1;
    args1 = malloc(sizeof(t_args));
    args1->sock1 = sock1;
    args1->sock2 = sock2;
    args1->game_object = game_object;
    strcpy(args1->client_name, client1_addr);
    strcpy(args1->client_port, client1_port);

    t_args* args2;
    args2 = malloc(sizeof(t_args));
    args2->sock1 = sock1;
    args2->sock2 = sock2;
    args2->game_object = game_object;
    strcpy(args2->client_name, client2_addr);
    strcpy(args2->client_port, client2_port);

    pthread_t p1_thread, p2_thread;
    args1->thread1 = &p1_thread;
    args1->thread2 = &p2_thread;
    args2->thread1 = &p1_thread;
    args2->thread2 = &p2_thread;
    if(pthread_create(&p1_thread, NULL, socket1_handler, args1) != 0) {
        printf("Failed to create player1 thread.\n");
        exit(EXIT_FAILURE);
    }
    if(pthread_create(&p2_thread, NULL, socket2_handler, args2) != 0) {
        printf("Failed to create player2 thread.\n");
        exit(EXIT_FAILURE);
    }
    if(pthread_join(p1_thread, NULL) != 0) {
        printf("Failed to join player1 thread.\n");
        exit(EXIT_FAILURE);
    }
    if(pthread_join(p2_thread, NULL) != 0) {
        printf("Failed to join player2 thread.\n");
        exit(EXIT_FAILURE);
    }

    printf("GAME ENDED\n");
    free(args1);
    free(args2);
    
}
void* start_game(void* arg){
    /*
    if(wait_play(game_object->clients[0], game_object->clients[1]) == 1) {
        respond2(clients[0], clients[1], (struct sockaddr*)&strg_list[0], (struct sockaddr*)&strg_list[1], len_list[0], len_list[1]);
    }*/
    game* game_object = (game*) arg;
    if(wait_play(game_object) == 1) {
        respond2(game_object);
    }
    // touch up
    if(game_object->player1->name != NULL) {
        remove_username(game_object->player1->name);
        free(game_object->player1->name);
    }
    if(game_object->player2->name != NULL) {
        remove_username(game_object->player2->name);
        free(game_object->player2->name);
    }
    free(game_object->player1);
    free(game_object->player2);
    close(game_object->clients[0]);
    close(game_object->clients[1]);
    free(game_object);

    pthread_exit(NULL);
}
int main(int argc, char** argv) {
    //  Create lock
    if (pthread_mutex_init(&username_list_lock, NULL) != 0){
        perror("mutex_init");
        return 1;
    }

    struct sockaddr_storage client;
    socklen_t client_length;
    int client_cnt = 0;
    int clients[2];
    struct sockaddr_storage strg_list[2];
    socklen_t len_list[2];

    char* port = (argc > 1) ? argv[1] : "21888";
    int listener = initialize(port);
    if(listener < 0) {
        printf("Error: Unable to obtain listener.\n");
        return 1;
    }
    printf("Server started!\n");
    while(active) {
        client_length = sizeof(client);
        int sock = accept(listener, (struct sockaddr*)&client, &client_length);
        if(sock < 0) {
            printf("Error: Unable to accept connection.\n");
            continue;
        }
        if (client_cnt == 1){
            //  A client is currently connected and waiting to play
            //  Make sure they're still connected
            write(clients[0], "ping", 5);
            char ping_response_buff[8];
            if (read(clients[0], ping_response_buff, 8) == 0){
                //  Client disconnected, reset client_cnt
                client_cnt = 0;
                close(clients[0]);
            }
        }
        // pair up connections
        clients[client_cnt] = sock;
        strg_list[client_cnt] = client;
        len_list[client_cnt] = client_length;
        client_cnt++;

        if(client_cnt % 2 == 0) {
            client_cnt = 0;

            game* game_object = malloc(sizeof(game));
            game_object->player1 = malloc(sizeof(player));
            game_object->player1->role = '.';
            game_object->player2 = malloc(sizeof(player));
            game_object->player2->role = '.';
            game_object->clients[0] = clients[0];
            game_object->clients[1] = clients[1];
            game_object->strg_list[0] = &strg_list[0];
            game_object->strg_list[1] = &strg_list[1];
            game_object->len_list[0] = len_list[0];
            game_object->len_list[0] = len_list[1];
            strcpy(game_object->board, ".........");
            game_object->draw_requester = -1;

            if (pthread_mutex_init(&game_object->lock, NULL) != 0){
                perror("mutex_init");
                return 1;
            }

            pthread_t game_thread;

            if(pthread_create(&game_thread, NULL, start_game, game_object) != 0) {
                printf("Failed to create game thread.\n");
                return 1;
            }
        }
    }
    free_un_list(username_head);
    //  Destroy lock
    pthread_mutex_destroy(&username_list_lock);

    return 0;
}