#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

#define PORT 9090
#define BUFFER_SIZE 1024

char *run_length_encode(const char *data, long *encoded_size)
{
    long len = strlen(data);
    char *encoded = malloc(2 * len + 1); 
    if (!encoded)
    {
        perror("Memory allocation failed");
        return NULL;
    }

    char *ptr = encoded;
    for (long i = 0; i < len; i++)
    {
        int count = 1;
        while (i + 1 < len && data[i] == data[i + 1])
        {
            count++;
            i++;
        }
        *ptr++ = data[i];
        *ptr++ = count + '0'; 
    }
    *ptr = '\0';
    *encoded_size = ptr - encoded;
    return encoded;
}

char *run_length_decode(const char *data, long *decoded_size)
{
    long len = strlen(data);
    char *decoded = malloc(len * 9 + 1); 
    if (!decoded)
    {
        perror("Memory allocation failed");
        return NULL;
    }

    char *ptr = decoded;
    for (long i = 0; i < len; i += 2)
    {
        char ch = data[i];
        int count = data[i + 1] - '0'; 
        for (int j = 0; j < count; j++)
        {
            *ptr++ = ch;
        }
    }
    *ptr = '\0';
    *decoded_size = ptr - decoded;
    return decoded;
}

void handle_upload(int client_fd, char *command)
{
    char file_path[BUFFER_SIZE] = {0};
    char buffer[BUFFER_SIZE] = {0};
    long valread;

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
        return;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *file_data = malloc(file_size + 1);
    if (file_data == NULL)
    {
        perror("Memory allocation failed");
        fclose(fp);
        return;
    }

    fread(file_data, 1, file_size, fp);
    fclose(fp);

    file_data[file_size] = '\0'; 

    long encoded_size;
    char *encoded_data = run_length_encode(file_data, &encoded_size);
    if (!encoded_data)
    {
        free(file_data);
        return;
    }

    free(file_data);

    send(client_fd, &encoded_size, sizeof(encoded_size), 0);
    printf("Sending encoded file size: %ld bytes\n", encoded_size);

    valread = read(client_fd, buffer, sizeof(buffer) - 1);
    buffer[valread] = '\0';
    printf("Server response: %s\n", buffer);

    send(client_fd, encoded_data, encoded_size, 0);

    free(encoded_data);
    printf("File %s uploaded successfully (encoded).\n", file_path);
}

void handle_download(int client_fd, char *command)
{
    char file_path[BUFFER_SIZE] = {0};
    char buffer[BUFFER_SIZE] = {0};
    long valread;

    char *file_path_start = command + 10;
    char *file_path_end = strrchr(file_path_start, '$');
    if (file_path_end != NULL)
    {
        *file_path_end = '\0';
    }
    strncpy(file_path, file_path_start, sizeof(file_path) - 1);

    printf("Requesting to download file: %s\n", file_path);

    valread = read(client_fd, buffer, sizeof(buffer) - 1);
    buffer[valread] = '\0';

    if (strncmp(buffer, "$SUCCESS$FILE_FOUND$", 19) == 0)
    {
        long encoded_size = 0;
        valread = read(client_fd, &encoded_size, sizeof(encoded_size));
        if (valread <= 0)
        {
            printf("Failed to receive encoded file size.\n");
            return;
        }
        printf("Receiving encoded file of size: %ld bytes\n", encoded_size);

        char *encoded_data = malloc(encoded_size + 1);
        if (encoded_data == NULL)
        {
            perror("Memory allocation failed");
            return;
        }

        long bytes_received = 0;
        while (bytes_received < encoded_size)
        {
            valread = read(client_fd, encoded_data + bytes_received, encoded_size - bytes_received);
            if (valread <= 0)
            {
                break;
            }
            bytes_received += valread;
        }
        encoded_data[bytes_received] = '\0';

        long decoded_size;
        char *decoded_data = run_length_decode(encoded_data, &decoded_size);
        free(encoded_data);

        if (decoded_data == NULL)
        {
            return;
        }

        FILE *fp = fopen(file_path, "wb");
        if (fp == NULL)
        {
            perror("Failed to open file for writing");
            free(decoded_data);
            return;
        }

        fwrite(decoded_data, 1, decoded_size, fp);
        fclose(fp);
        free(decoded_data);

        if (bytes_received == encoded_size)
        {
            printf("File downloaded and decoded successfully: %s\n", file_path);
        }
        else
        {
            printf("File download incomplete. Expected: %ld bytes, Received: %ld bytes\n", encoded_size, bytes_received);
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

void handle_view(int client_fd)
{
    char buffer[BUFFER_SIZE] = {0};
    long valread = read(client_fd, buffer, sizeof(buffer) - 1);
    buffer[valread] = '\0';
    printf("Server response: %s\n", buffer);
}

void handle_logout(int client_fd)
{
    char buffer[BUFFER_SIZE] = {0};
    long valread = read(client_fd, buffer, sizeof(buffer) - 1);
    buffer[valread] = '\0';
    printf("Server response: %s\n", buffer);
    printf("Logging out.\n");
}

void handle_close(int client_fd)
{
    close(client_fd);
    printf("Closing connection.\n");
}

int main(int argc, char const *argv[])
{
    int client_fd, valread;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char command[BUFFER_SIZE] = {0};

    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address or Address not supported");
        close(client_fd);
        return -1;
    }

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

                send(client_fd, command, strlen(command), 0);
                printf("Command sent: %s\n", command);


                if (strncmp(command, "$CLOSE$", 7) == 0)
                {
                    handle_close(client_fd);
                    return 0;
                }
                else if (strncmp(command, "$UPLOAD$", 8) == 0)
                {
                    handle_upload(client_fd, command);
                }
                else if (strncmp(command, "$DOWNLOAD$", 10) == 0)
                {
                    handle_download(client_fd, command);
                }
                else if (strncmp(command, "$VIEW$", 6) == 0)
                {
                    handle_view(client_fd);
                }
                else if (strncmp(command, "$LOGOUT$", 8) == 0)
                {
                    handle_logout(client_fd);
                    break;
                }
            }
        }
        //Error Handling.
        else if (strncmp(buffer, "$FAILURE$LOGIN$", 15) == 0)
        {
            printf("Login failed.\n");
        }
        else if (strncmp(buffer, "$SUCCESS$REGISTER$", 18) == 0)
        {
            printf("Registration successful.\n");
        }
        else if (strncmp(buffer, "$FAILURE$REGISTER$", 18) == 0)
        {
            printf("Registration failed. Username already exists.\n");
        }
    }

    return 0;
}
