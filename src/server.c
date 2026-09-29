#include <stdio.h>      // printf, perror
#include <stdlib.h>     // exit, EXIT_SUCCESS, EXIT_FAILURE
#include <string.h>     // memset
#include <unistd.h>     // close

#include <arpa/inet.h>  // htons, htonl, inet_ntop
#include <netinet/in.h> // struct sockaddr_in, INADDR_ANY
#include <sys/socket.h> // socket, bind, listen, accept
#include <sys/types.h>  // basic system data types
#include <fcntl.h>

static void send_text(int client_fd, const char *status, const char *body){
    size_t body_len = strlen(body);

    char errhdr_buf[256];
    int errhdr_len = snprintf(errhdr_buf, sizeof(errhdr_buf),
                                "HTTP/1.1 %s\r\n"
                                "Content-type: text/plain; charset=utf-8\r\n"
                                "Content-Length: %zu\r\n"
                                "Connection: close\r\n"
                                "\r\n",
                                status, body_len);
    write(client_fd, errhdr_buf, errhdr_len);
    write(client_fd, body, body_len);
}

int main(void)
{
     printf("server: starting(no networking yet) \n");

     /* Create a TCP socket (IPv4)*/
     // AF_INET = IPv4
     //SOCK_STREAM = TCP/reliable byte sttream

     int server_fd = socket(AF_INET, SOCK_STREAM, 0); //creating socket, saying we want address family -> IPv4, and stating we want TCP

     //if socket() fails, it returns -1
     if(server_fd < 0)
     {
        perror("socket");
        return EXIT_FAILURE;
     }

     /*=================================================================================*/

     /* Set SO_REUSEADDR so we can restart the server quickly after it exits. */
     //setsockop = set socket options
     //SOL_SOCKET = specifying socket level
     //SO_REUSEADDR = Allow the socket to reuse the address/port even if it is in TIME_WAIT
     //optval controls whether SO_REUSEADDR is enabled (1 = ON, 0 = OFF)
     
     int optval = 1;
     if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) 
     {
        perror("setsockopt(SO_REUSEADDR)");
        close(server_fd);
        return EXIT_FAILURE;
    }

    /*=================================================================================*/

    /* Build the IPv4 address we want to bind to (0.0.0.0:8080). */
    // memset makes sure garbage values aren't there after addr struct is created
    //htonl = host to network long -- converts from host bit order to network bit order
    //htons = host to network short

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));          // clear the struct so unused bytes are 0

    addr.sin_family = AF_INET;               // IPv4 address family
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0 = all local interfaces (in network byte order)
    addr.sin_port = htons(8080);             // port 8080 (in network byte order)


    /*=================================================================================*/

    /* Bind the socket to that address+port. */
    /* This block asks the OS to associate your socket (server_fd) with the local IP address and port described by addr — and aborts cleanly if that fails.*/

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    /*=================================================================================*/
    /* Mark the socket as a listening socket (ready to accept connections). */
    /*Returns 0 on success, -1 on error*/
    /*The 16 is the backlog, controls the connections queue size -- "allow this # of connection requests to wait in line before I accept them"*/
    /* When listen() runs successfully it marks the socket as "listening", creates internal connection queues, starts handling TCP SYN packets, completes TCP handshakes in kernel space*/
    /* listen() turns a bound TCP socket into a server socket*/

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    /*=================================================================================*/
    /* Main server loop: accept and handle clients forever */

    for (;;) 
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                            (struct sockaddr *)&client_addr,
                            &client_len);

        if (client_fd < 0) {
            perror("accept");
            continue;   // don't exit the server, just try again
        }

        /* Read the client's HTTP request */
        char req_buf[4096];
        ssize_t nread = read(client_fd, req_buf, sizeof(req_buf) - 1);
        if (nread < 0) {
            perror("read");
            close(client_fd);
            continue;
        }

        req_buf[nread] = '\0';

        printf("----- HTTP request start -----\n");
        printf("%s", req_buf);
        printf("------ HTTP request end ------\n");

        char method[8];
        char path[1024];

        if(sscanf(req_buf, "%7s %1023s", method, path) != 2){
            printf("empty or malformed request, skipping\n");
            close(client_fd);
            continue;
        }

        //cutting string from "?" on, and replacing with "\0"
        char *query = strchr(path, '?');
            if(query != NULL){
                *query = '\0';
            }

        printf("method = [%s], path [%s]\n", method, path);

        if(strcmp(path, "/") == 0){
            
            /* Serve www/index.html */
            int file_fd = open("www/index.html", O_RDONLY);
            if (file_fd < 0) {
                perror("open(index.html)");
                close(client_fd); 
                continue;
            }

            char file_buf[32768];
            ssize_t file_size = read(file_fd, file_buf, sizeof(file_buf));
            close(file_fd);

            if (file_size < 0) 
            {
                perror("read(index.html)");
                close(client_fd);
                continue;
            }

            char header_buf[256];
            int header_len = snprintf(header_buf, sizeof(header_buf),
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: text/html; charset=utf-8\r\n"
                                    "Content-Length: %zd\r\n"
                                    "Connection: close\r\n"
                                    "\r\n",
                                    file_size);

            write(client_fd, header_buf, header_len);
            write(client_fd, file_buf, file_size);
        }
        else if(strncmp(path, "/captures/", 10) == 0){
            const char *filename = path + 10;
            if(strstr(filename, "..") != NULL){
                send_text(client_fd, "403 Forbidden", "Forbidden\n");
            }
            else{
                printf("image requested: [%s]\n", filename);
            }
            
        

        }
        else{
            printf("no route for %s\n", path);
            send_text(client_fd, "404 Not Found", "Not found\n");

        }

        close(client_fd);
    }
}