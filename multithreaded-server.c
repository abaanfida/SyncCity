#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h> 
#include <semaphore.h>

#define PORT 9090
#define BUFFER_SIZE 1024
#define MAX_STORAGE 50 
#define QUEUE_SIZE 100
#define MAX_USERS 50

typedef struct 
{
    int socket;
    char command[BUFFER_SIZE];
} task_t;

typedef struct 
{
    task_t tasks[QUEUE_SIZE];
    int front;
    int rear;
    int count;
    pthread_mutex_t lock;
    sem_t full; 
    sem_t empty; 
} queue_t;


void queue_init(queue_t *queue) 
{
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;
    pthread_mutex_init(&queue->lock, NULL);
    sem_init(&queue->full, 0, 0);         
    sem_init(&queue->empty, 0, QUEUE_SIZE);
}


void enqueue(queue_t *queue, task_t task) 
{
    sem_wait(&queue->empty);          
    pthread_mutex_lock(&queue->lock); 

    queue->tasks[queue->rear] = task;
    queue->rear = (queue->rear + 1) % QUEUE_SIZE;
    queue->count++;

    pthread_mutex_unlock(&queue->lock);
    sem_post(&queue->full);           
}


task_t dequeue(queue_t *queue) 
{
    sem_wait(&queue->full);           
    pthread_mutex_lock(&queue->lock);  
    task_t task = queue->tasks[queue->front];
    queue->front = (queue->front + 1) % QUEUE_SIZE;
    queue->count--;

    pthread_mutex_unlock(&queue->lock);
    sem_post(&queue->empty);           
    return task;
}

queue_t task_queue;

void initialize_server() 
{
    queue_init(&task_queue);
}





typedef struct 
{
    int socket;
    struct sockaddr_in address;
    char username[BUFFER_SIZE];
} client_data_t;

void extract_filename(const char *path, char *filename)
{
    const char *last_slash = strrchr(path, '/');
    if (last_slash != NULL)
    {
        strcpy(filename, last_slash + 1);
    }
    else
    {
        strcpy(filename, path);
    }
}

int username_exists(const char *username)
{
    FILE *file = fopen("users.txt", "r");
    if (file == NULL)
    {
        perror("Error opening users.txt for reading");
        return 0; 
    }

    char file_username[BUFFER_SIZE];
    char file_password[BUFFER_SIZE];
    while (fscanf(file, "%s %s", file_username, file_password) != EOF)
    {
        if (strcmp(username, file_username) == 0)
        {
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    return 0;
}

void register_user(const char *username, const char *password)
{
    FILE *file = fopen("users.txt", "a");
    if (file != NULL)
    {
        fprintf(file, "%s %s\n", username, password);
        fclose(file);

        char user_folder[BUFFER_SIZE];
        snprintf(user_folder, sizeof(user_folder), "%s_files", username);
        mkdir(user_folder, 0700);

        printf("User registered successfully.\n");
    }
    else
    {
        perror("Error opening users.txt for writing");
    }
}

int authenticate_user(const char *username, const char *password)
{
    FILE *file = fopen("users.txt", "r");
    if (file == NULL)
    {
        perror("Error opening users.txt for reading");
        return 0;
    }

    char file_username[BUFFER_SIZE], file_password[BUFFER_SIZE];
    while (fscanf(file, "%s %s", file_username, file_password) != EOF)
    {
        if (strcmp(username, file_username) == 0 && strcmp(password, file_password) == 0)
        {
            fclose(file);
            return 1; 
        }
    }

    fclose(file);
    return 0; 
}

void format_file_info(const char *filename, struct stat *file_stat, char *output)
{
    char time_str[50];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&file_stat->st_mtime));
    snprintf(output, BUFFER_SIZE, "%s | %s | %ld bytes", filename, time_str, file_stat->st_size);
}



long calculate_directory_size(const char *dir_path)
{
    long total_size = 0;
    struct dirent *entry;
    struct stat file_stat;
    DIR *dir = opendir(dir_path);

    if (dir == NULL)
    {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char full_path[BUFFER_SIZE];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode))
        {
            total_size += file_stat.st_size;
        }
    }

    closedir(dir);
    return total_size;
}

void handle_register_command(int socket, char *command) 
{
    char username[BUFFER_SIZE] = {0};
    char password[BUFFER_SIZE] = {0};
    char *credentials = command + 10;
    char *username_end = strchr(credentials, '$');
    
    if (username_end != NULL) 
    {
        *username_end = '\0';
        strncpy(username, credentials, sizeof(username) - 1);
        credentials = username_end + 1;
        char *password_end = strchr(credentials, '$');
        
        if (password_end != NULL) 
        {
            *password_end = '\0';
            strncpy(password, credentials, sizeof(password) - 1);
            if (username_exists(username)) 
            {
                send(socket, "$FAILURE$USER_EXISTS$", strlen("$FAILURE$USER_EXISTS$"), 0);
            } 
            else 
            {
                register_user(username, password);
                send(socket, "$SUCCESS$REGISTER$", strlen("$SUCCESS$REGISTER$"), 0);
            }
        }
    }
}

void handle_login_command(int socket, char *command,char* username) 
{
    char password[BUFFER_SIZE] = {0};
    char *credentials = command + 7;
    char *username_end = strchr(credentials, '$');
    
    if (username_end != NULL) 
    {
        *username_end = '\0';
        strncpy(username, credentials, sizeof(username) - 1);
        credentials = username_end + 1;
        char *password_end = strchr(credentials, '$');
        
        if (password_end != NULL) 
        {
            *password_end = '\0';
            strncpy(password, credentials, sizeof(password) - 1);
            if (authenticate_user(username, password)) 
            {
                send(socket, "$SUCCESS$LOGIN$", strlen("$SUCCESS$LOGIN$"), 0);
                printf("Client Logged In\n");
            } 
            else 
            {
                send(socket, "$FAILURE$LOGIN$", strlen("$FAILURE$LOGIN$"), 0);
            }
        }
    }
}

void handle_upload_command(int socket, char *command, const char *username)
{
    char file_path[BUFFER_SIZE] = {0};
    char filename[BUFFER_SIZE] = {0};
    char buffer[BUFFER_SIZE] = {0};

    char *file_path_start = command + 8;
    char *file_path_end = strrchr(file_path_start, '$');
    if (file_path_end != NULL) {
        *file_path_end = '\0';
    }
    strncpy(file_path, file_path_start, sizeof(file_path) - 1);
    printf("Client wants to upload file from path: %s\n", file_path);

    extract_filename(file_path, filename);
    char user_folder[BUFFER_SIZE];
    snprintf(user_folder, sizeof(user_folder), "%s_files", username);
    snprintf(file_path, sizeof(file_path), "%s/%s", user_folder, filename);
    printf("Extracted file name: %s\n", filename);

    FILE *fp = fopen(file_path, "w");
    if (fp == NULL) {
        perror("Failed to open file");
        return;
    }
    long file_size = 0;
    int valread = read(socket, &file_size, sizeof(file_size));
    if(file_size==-1)
    {
        printf("Client Could Not Open File!\n");
        return;
    }
    if (valread <= 0) {
        perror("Failed to read file size");
        fclose(fp);
        return;
    }
    printf("Receiving file of size: %ld bytes\n", file_size);

    long dir_size = calculate_directory_size(user_folder);
    if (dir_size < 0) 
    {
        perror("Failed to calculate directory size");
        send(socket, "$ERROR$", strlen("$ERROR$"), 0);
        fclose(fp);
        return;
    }
    printf("Total size of directory %s: %ld bytes\n", user_folder, dir_size);
    if (dir_size + file_size > MAX_STORAGE * 1024) 
    {
        perror("Low User Storage");
        send(socket, "$LOW_SPACE$", strlen("$LOW_SPACE$"), 0);
        fclose(fp);
        return;
    }
    
    long bytes_received = 0;
    while (bytes_received < file_size && (valread = read(socket, buffer, sizeof(buffer))) > 0) 
    {
        fwrite(buffer, sizeof(char), valread, fp);
        bytes_received += valread;
    }

    fclose(fp);
    if (bytes_received == file_size) 
    {
        printf("File %s uploaded successfully.\n", file_path);
        send(socket, "$SUCCESS$", strlen("$SUCCESS$"), 0);

    }
    else 
    {
        printf("File upload incomplete. Expected: %ld bytes, Received: %ld bytes\n", file_size, bytes_received);
    }
}


void handle_download_command(int socket, char *command, const char *username) 
{
    char file_path[BUFFER_SIZE] = {0};
    char filename[BUFFER_SIZE] = {0};
    char buffer[BUFFER_SIZE] = {0};

    char *file_path_start = command + 10;
    char *file_path_end = strrchr(file_path_start, '$');
    if (file_path_end != NULL) 
    {
        *file_path_end = '\0';
    }
    strncpy(file_path, file_path_start, sizeof(file_path) - 1);

    //extract filename
    extract_filename(file_path, filename);

    char user_folder[BUFFER_SIZE];
    snprintf(user_folder, sizeof(user_folder), "%s_files", username);


    char full_file_path[BUFFER_SIZE];
    snprintf(full_file_path, sizeof(full_file_path), "%s/%s", user_folder, filename);


    struct stat file_stat;
    if (stat(full_file_path, &file_stat) == 0) 
    {
        printf("File found: %s\n", full_file_path);

        // Open the file for reading
        FILE *fp = fopen(full_file_path, "rb");
        if (fp != NULL) 
        {
            send(socket, "$SUCCESS$FILE_FOUND$", strlen("$SUCCESS$FILE_FOUND$"), 0);

            // Send the file size
            fseek(fp, 0L, SEEK_END);
            long file_size = ftell(fp);
            rewind(fp);
            send(socket, &file_size, sizeof(file_size), 0);


            long bytes_sent = 0;
            int valread;
            while ((valread = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                send(socket, buffer, valread, 0);
                bytes_sent += valread;
            }
            fclose(fp);
            printf("File sent successfully: %s, bytes: %ld\n", filename, bytes_sent);
        }
        else 
        {
            perror("Failed to open file for reading");
            send(socket, "$FAILURE$CANNOT_OPEN_FILE$", strlen("$FAILURE$CANNOT_OPEN_FILE$"), 0);
        }
    } 
    else 
    {
        printf("File not found: %s\n", full_file_path);
        send(socket, "$FAILURE$FILE_NOT_FOUND$", strlen("$FAILURE$FILE_NOT_FOUND$"), 0);
    }
}

void handle_view_command(int socket, const char *username)
{
    char user_folder[BUFFER_SIZE];
    snprintf(user_folder, sizeof(user_folder), "%s_files", username);

    DIR *dir = opendir(user_folder);
    if (dir == NULL)
    {
        send(socket, "$FAILURE$NO_CLIENT_DATA$", strlen("$FAILURE$NO_CLIENT_DATA$"), 0);
        return;
    }

    struct dirent *entry;
    struct stat file_stat;
    char file_info[BUFFER_SIZE];
    char response[BUFFER_SIZE * 10] = ""; 

    while ((entry = readdir(dir)) != NULL)
    {

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // Get the full path for the file
        char full_path[BUFFER_SIZE];
        snprintf(full_path, sizeof(full_path), "%s/%s", user_folder, entry->d_name);

        // Get file statistics
        if (stat(full_path, &file_stat) == 0)
        {
            format_file_info(entry->d_name, &file_stat, file_info);
            strncat(response, file_info, sizeof(response) - strlen(response) - 1);
            strncat(response, "\n", sizeof(response) - strlen(response) - 1);
        }
    }

    closedir(dir);

    if (strlen(response) == 0)
    {
        send(socket, "$FAILURE$NO_CLIENT_DATA$", strlen("$FAILURE$NO_CLIENT_DATA$"), 0);
    }
    else
    {
        send(socket, response, strlen(response), 0);
    }
}

void handle_logout_command(int socket) 
{
    printf("Client logged out.\n");
    send(socket, "$SUCCESS$LOGOUT$", strlen("$SUCCESS$LOGOUT$"), 0);
}

void handle_close_command(int socket) 
{
    printf("Closing connection as requested by the client.\n");
}


client_data_t *client_data_list[MAX_USERS];
client_data_t *get_client_data_by_socket(int socket) 
{
    for (int i = 0; i < MAX_USERS; i++) {
        if (client_data_list[i] != NULL && client_data_list[i]->socket == socket) 
        {
            return client_data_list[i];
        }
    }
    return NULL; // Not found
}



void *handle_client(void *arg) {
    client_data_t *client_data = (client_data_t *)arg;
    int new_socket = client_data->socket;
    char command[BUFFER_SIZE] = {0};

    printf("Handling new client in thread.\n");

    while (1) 
    {
        int valread = read(new_socket, command, sizeof(command) - 1);
        if (valread <= 0) 
        {
            printf("Client disconnected or read error.\n");
            break;
        }

        command[valread] = '\0';

        task_t task;
        task.socket = new_socket;
        strncpy(task.command, command, sizeof(task.command) - 1);

        enqueue(&task_queue, task); // Add task to the queue
    }

    close(new_socket);
    free(client_data); // Free allocated memory
    printf("Client connection closed.\n");
    pthread_exit(NULL);
}

void *file_handler(void *arg) 
{
    while (1) {
        task_t task = dequeue(&task_queue); 

        client_data_t *client_data = get_client_data_by_socket(task.socket);


        if (client_data == NULL) 
        {
            continue;  
        }

        if (strncmp(task.command, "$REGISTER$", 10) == 0) {
            handle_register_command(task.socket, task.command);
        } else if (strncmp(task.command, "$LOGIN$", 7) == 0) {
            handle_login_command(task.socket, task.command, client_data->username);
        } else if (strncmp(task.command, "$UPLOAD$", 8) == 0) {
            handle_upload_command(task.socket, task.command, client_data->username);
        } else if (strncmp(task.command, "$VIEW$", 6) == 0) {
            handle_view_command(task.socket, client_data->username);
        } else if (strncmp(task.command, "$DOWNLOAD$", 10) == 0) {
            handle_download_command(task.socket, task.command, client_data->username);
        } else if (strncmp(task.command, "$LOGOUT$", 8) == 0) {
            handle_logout_command(task.socket);
        } else if (strncmp(task.command, "$CLOSE$", 7) == 0) {
            printf("Client requested connection close.\n");
            close(task.socket);
        } else {
            send(task.socket, "$ERROR$INVALID_COMMAND$", strlen("$ERROR$INVALID_COMMAND$"), 0);
        }
    }
    pthread_exit(NULL);
}




int main(int argc, char const *argv[])
{
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 9090
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Binding the socket to port 9090
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listening for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening for connections...\n");

    initialize_server();

    pthread_t file_handler_thread;
    pthread_create(&file_handler_thread, NULL, file_handler, NULL);

    while (1) {
        // Accept new clients and spawn threads
        client_data_t *client_data = malloc(sizeof(client_data_t));
        client_data->socket = accept(server_fd, (struct sockaddr *)&client_data->address, &addrlen);
        client_data_list[client_data->socket] = client_data;  // Store the client data


        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handle_client, client_data);
        pthread_detach(client_thread); // Automatically clean up after client thread exits
    }

    pthread_join(file_handler_thread, NULL);
    close(server_fd);
    return 0;
}
