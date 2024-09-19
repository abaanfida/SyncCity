#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

#define PORT 9090
#define BUFFER_SIZE 1024

int main(int argc, char const *argv[])
{
    int client_fd, valread;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char command[BUFFER_SIZE] = {0};
    char file_path[BUFFER_SIZE] = {0};
    char username[BUFFER_SIZE] = {0};
    char password[BUFFER_SIZE] = {0};

    // Create socket
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address or Address not supported");
        close(client_fd);
        return -1;
    }

    // Connect to the server
    if (connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection failed");
        close(client_fd);
        return -1;
    }

    printf("Connected to the server.\n");

    while (1)
    {
        printf("Enter command ($REGISTER$<username>$<password>$ or $LOGIN$<username>$<password>$): ");
        scanf("%1023s", command);

        send(client_fd, command, strlen(command), 0);
        printf("Command sent: %s\n", command);

        valread = read(client_fd, buffer, sizeof(buffer) - 1);
        buffer[valread] = '\0';
        printf("Server response: %s\n", buffer);

        if (strncmp(buffer, "$SUCCESS$LOGIN$", 15) == 0)
        {
            printf("Logged in successfully.\n");

            while (1)
            {
                printf("Enter command ($UPLOAD$<file_path>$, $VIEW$, $DOWNLOAD$<file_name>$, $LOGOUT$ or $CLOSE$): ");
                scanf("%1023s", command);

                if (strncmp(command, "$CLOSE$", 7) == 0)
                {
                    send(client_fd, "$CLOSE$", strlen("$CLOSE$"), 0);
                    close(client_fd);
                    printf("Closing connection.\n");
                    return 0;
                }

                send(client_fd, command, strlen(command), 0);
                printf("Command sent: %s\n", command);

                valread = read(client_fd, buffer, sizeof(buffer) - 1);
                buffer[valread] = '\0';
                printf("Server response: %s\n", buffer);

                if (strncmp(command, "$UPLOAD$", 8) == 0)
                {
                    char *file_path_start = command + 8;
                    char *file_path_end = strrchr(file_path_start, '$');
                    if (file_path_end != NULL)
                    {
                        *file_path_end = '\0';
                    }
                    snprintf(file_path, sizeof(file_path), "%s", file_path_start);

                    FILE *fp = fopen(file_path, "rb");
                    if (fp == NULL)
                    {
                        perror("Failed to open file");
                        continue;
                    }

                    // Get the file size
                    fseek(fp, 0, SEEK_END);
                    long file_size = ftell(fp);
                    fseek(fp, 0, SEEK_SET);

                    // Send the file size
                    send(client_fd, &file_size, sizeof(file_size), 0);
                    printf("Sending file size: %ld bytes\n", file_size);

                    // Send the file contents
                    while ((valread = fread(buffer, sizeof(char), sizeof(buffer), fp)) > 0)
                    {
                        send(client_fd, buffer, valread, 0);
                    }

                    fclose(fp);
                    printf("File %s uploaded successfully.\n", file_path);
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

                    printf("Requesting to download file: %s\n", file_path);

                    if (strncmp(buffer, "$SUCCESS$FILE_FOUND$", 19) == 0)
                    {
                        // Receive the file size
                        long file_size = 0;
                        valread = read(client_fd, &file_size, sizeof(file_size));
                        if (valread <= 0)
                        {
                            printf("Failed to receive file size.\n");
                            return;
                        }
                        printf("Receiving file of size: %ld bytes\n", file_size);

                        // Open a file for writing
                        FILE *fp = fopen(file_path, "wb");
                        if (fp == NULL)
                        {
                            perror("Failed to open file for writing");
                            return;
                        }

                        // Receive the file contents
                        long bytes_received = 0;
                        while (bytes_received < file_size)
                        {
                            valread = read(client_fd, buffer, sizeof(buffer));
                            if (valread <= 0)
                            {
                                break; // Connection lost or error during reading
                            }
                            fwrite(buffer, sizeof(char), valread, fp);
                            bytes_received += valread;
                        }
                        fclose(fp);

                        if (bytes_received == file_size)
                        {
                            printf("File downloaded successfully: %s, bytes: %ld\n", file_path, bytes_received);
                        }
                        else
                        {
                            printf("File download incomplete. Expected: %ld bytes, Received: %ld bytes\n", file_size, bytes_received);
                        }
                    }
                    else if (strncmp(buffer, "$FAILURE$FILE_NOT_FOUND$", 24) == 0)
                    {
                        printf("The requested file was not found on the server.\n");
                    }
                    else
                    {
                        printf("An error occurred: %s\n", buffer);
                    }
                }

                else if (strncmp(command, "$VIEW$", 6) == 0)
                {
                    //already handled in the response statement outside ifs
                }
                else if (strncmp(command, "$LOGOUT$", 8) == 0)
                {
                    printf("Logging out.\n");
                    break;
                }
            }
        }
        else if (strncmp(buffer, "$FAILURE$LOGIN$", 15) == 0)
        {
            printf("Login failed.\n");
        }
    }

    close(client_fd);

    return 0;
}