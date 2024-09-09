#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PORT 9090
#define BUFFER_SIZE 1024

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
        return 0; // Assume no users exist if the file can't be opened
    }

    char file_username[BUFFER_SIZE];
    char file_password[BUFFER_SIZE];
    while (fscanf(file, "%s %s", file_username, file_password) != EOF)
    {
        if (strcmp(username, file_username) == 0)
        {
            fclose(file);
            return 1; // Username exists
        }
    }

    fclose(file);
    return 0; // Username does not exist
}

void register_user(const char *username, const char *password)
{
    FILE *file = fopen("users.txt", "a");
    if (file != NULL)
    {
        fprintf(file, "%s %s\n", username, password);
        fclose(file);

        // Create user folder
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
            return 1; // Authentication successful
        }
    }

    fclose(file);
    return 0; // Authentication failed
}

int main(int argc, char const *argv[])
{
    int server_fd, new_socket;
    int valread;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    char command[BUFFER_SIZE] = {0};
    char file_path[BUFFER_SIZE] = {0};
    char filename[BUFFER_SIZE] = {0};
    char username[BUFFER_SIZE] = {0};
    char password[BUFFER_SIZE] = {0};

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 9090
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Binding the socket to port 9090
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listening for incoming connections
    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while (1) // Server loop to accept multiple connections
    {
        printf("Waiting for a connection...\n");
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0)
        {
            perror("accept");
            continue; // Continue accepting other connections
        }

        printf("Connection accepted.\n");

        while (1) // Client handling loop
        {
            valread = read(new_socket, command, sizeof(command) - 1);
            if (valread <= 0)
            {
                printf("Client disconnected or read error.\n");
                break; // Exit the client handling loop
            }

            command[valread] = '\0';

            if (strncmp(command, "$REGISTER$", 10) == 0)
            {
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
                            send(new_socket, "$FAILURE$USER_EXISTS$", strlen("$FAILURE$USER_EXISTS$"), 0);
                        }
                        else
                        {
                            register_user(username, password);
                            send(new_socket, "$SUCCESS$REGISTER$", strlen("$SUCCESS$REGISTER$"), 0);
                        }
                    }
                }
            }
            else if (strncmp(command, "$LOGIN$", 7) == 0)
            {
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
                            send(new_socket, "$SUCCESS$LOGIN$", strlen("$SUCCESS$LOGIN$"), 0);
                            printf("Client Logged In\n");
                        }
                        else
                        {
                            send(new_socket, "$FAILURE$LOGIN$", strlen("$FAILURE$LOGIN$"), 0);
                        }
                    }
                }
            }
            else if (strncmp(command, "$UPLOAD$", 8) == 0)
            {
                char *file_path_start = command + 8;
                char *file_path_end = strrchr(file_path_start, '$');
                if (file_path_end != NULL)
                {
                    *file_path_end = '\0';
                }
                strncpy(file_path, file_path_start, sizeof(file_path) - 1);
                printf("Client wants to upload file from path: %s\n", file_path);

                extract_filename(file_path, filename);
                char user_folder[BUFFER_SIZE];
                snprintf(user_folder, sizeof(user_folder), "%s_files", username);
                snprintf(file_path, sizeof(file_path), "%s/%s", user_folder, filename);
                printf("Extracted file name: %s\n", filename);

                send(new_socket, "$SUCCESS$", strlen("$SUCCESS$"), 0);

                // Open file for writing
                FILE *fp = fopen(file_path, "w");
                if (fp == NULL)
                {
                    perror("Failed to open file");
                    continue;
                }

                // Read file size from the client
                long file_size = 0;
                valread = read(new_socket, &file_size, sizeof(file_size));
                if (valread <= 0)
                {
                    perror("Failed to read file size");
                    fclose(fp);
                    continue;
                }
                printf("Receiving file of size: %ld bytes\n", file_size);

                // Read the file contents based on the file size
                long bytes_received = 0;
                while (bytes_received < file_size && (valread = read(new_socket, buffer, sizeof(buffer))) > 0)
                {
                    fwrite(buffer, sizeof(char), valread, fp);
                    bytes_received += valread;
                }

                fclose(fp);
                if (bytes_received == file_size)
                {
                    printf("File %s uploaded successfully.\n", file_path);
                }
                else
                {
                    printf("File upload incomplete. Expected: %ld bytes, Received: %ld bytes\n", file_size, bytes_received);
                }
            }

            else if (strncmp(command, "$VIEW$", 6) == 0)
            {
                // to be added
            }
            else if (strncmp(command, "$DOWNLOAD$", 10) == 0)
            {
                char *file_path_start = command + 10;
                char *file_path_end = strrchr(file_path_start, '$');
                if (file_path_end != NULL)
                {
                    *file_path_end = '\0';
                }
                strncpy(file_path, file_path_start, sizeof(file_path) - 1);

                extract_filename(file_path, filename);

                // Construct the user folder path
                char user_folder[BUFFER_SIZE];
                snprintf(user_folder, sizeof(user_folder), "%s_files", username);

                // Construct the full path to the file in the user's folder
                char full_file_path[BUFFER_SIZE];
                snprintf(full_file_path, sizeof(full_file_path), "%s/%s", user_folder, filename);

                // Check if the file exists
                struct stat file_stat;
                if (stat(full_file_path, &file_stat) == 0)
                {
                    // File exists, proceed with sending it to the client
                    printf("File found: %s\n", full_file_path);

                    // Open the file for reading
                    FILE *fp = fopen(full_file_path, "rb");
                    if (fp != NULL)
                    {
                        // Send a success message indicating that the file was found
                        send(new_socket, "$SUCCESS$FILE_FOUND$", strlen("$SUCCESS$FILE_FOUND$"), 0);

                        // Send the file size
                        fseek(fp, 0L, SEEK_END);
                        long file_size = ftell(fp);
                        rewind(fp);
                        send(new_socket, &file_size, sizeof(file_size), 0);

                        // Send the file contents in chunks
                        long bytes_sent = 0;
                        while ((valread = fread(buffer, 1, sizeof(buffer), fp)) > 0)
                        {
                            send(new_socket, buffer, valread, 0);
                            bytes_sent += valread;
                        }
                        fclose(fp);
                        printf("File sent successfully: %s, bytes: %ld\n", filename, bytes_sent);
                    }
                    else
                    {
                        perror("Failed to open file for reading");
                        send(new_socket, "$FAILURE$CANNOT_OPEN_FILE$", strlen("$FAILURE$CANNOT_OPEN_FILE$"), 0);
                    }
                }
                else
                {
                    // File does not exist
                    printf("File not found: %s\n", full_file_path);
                    send(new_socket, "$FAILURE$FILE_NOT_FOUND$", strlen("$FAILURE$FILE_NOT_FOUND$"), 0);
                }
            }

            else if (strncmp(command, "$LOGOUT$", 8) == 0)
            {
                printf("Client logged out.\n");
                send(new_socket, "$SUCCESS$LOGOUT$", strlen("$SUCCESS$LOGOUT$"), 0);
                break;
            }
            else if (strncmp(command, "$CLOSE$", 7) == 0)
            {
                printf("Closing connection as requested by the client.\n");
                break;
            }
            else
            {
                send(new_socket, "$FAILURE$UNKNOWN_COMMAND$", strlen("$FAILURE$UNKNOWN_COMMAND$"), 0);
            }
        }

        close(new_socket);
    }

    close(server_fd);
    return 0;
}