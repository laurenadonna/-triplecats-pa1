/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here
# Student #1: Joyce Yang
# Student #2: Lauren Hensley
# Student #3: Patricia Luna
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
} client_thread_data_t;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;

    // Create socket
    data->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (data->socket_fd < 0) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    // Connect to server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (connect(data->socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(data->socket_fd);
        pthread_exit(NULL);
    }

    // Create epoll instance
    data->epoll_fd = epoll_create1(0);
    if (data->epoll_fd < 0) {
        perror("Epoll creation failed");
        close(data->socket_fd);
        pthread_exit(NULL);
    }

    // Register socket with epoll
    event.events = EPOLLIN;
    event.data.fd = data->socket_fd;
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event) < 0) {
        perror("Epoll control failed");
        close(data->socket_fd);
        close(data->epoll_fd);
        pthread_exit(NULL);
    }

    for (int i = 0; i < num_requests; i++) {
        // Record start time
        gettimeofday(&start, NULL);

        // Send message to server
        if (send(data->socket_fd, send_buf, MESSAGE_SIZE, 0) < 0) {
            perror("Send failed");
            break;
        }
        printf("Client: Sent message '%s'\n", send_buf);

        // Wait for response using epoll
        int n = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            perror("Epoll wait failed");
            break;
        }

        // Receive response from server
        if (recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0) < 0) {
            perror("Receive failed");
            break;
        }
        printf("Client: Received message '%s'\n", recv_buf);

        // Record end time
        gettimeofday(&end, NULL);

        // Calculate RTT
        long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
        data->total_rtt += rtt;
        data->total_messages++;
        printf("Client: RTT for message %d: %lld us\n", i + 1, rtt);
    }

    // Calculate request rate
    data->request_rate = (float)data->total_messages / (data->total_rtt / 1000000.0);

    // Clean up
    close(data->socket_fd);
    close(data->epoll_fd);

    return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0.0;

    // Initialize thread data and create threads
    for (int i = 0; i < num_client_threads; i++) {
        memset(&thread_data[i], 0, sizeof(client_thread_data_t));
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    // Wait for threads to complete and aggregate metrics
    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL);
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;
    }

    // Calculate overall metrics
    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
}

void run_server() {
    int listen_fd, epoll_fd;
    struct epoll_event event, events[MAX_EVENTS];
    struct sockaddr_in server_addr;
    int tr = 1;

    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &tr, sizeof(int)) == -1) {
        perror("Error, setsockopt");
        exit(EXIT_FAILURE);
    }

    // Bind socket to IP and port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(listen_fd, SOMAXCONN) < 0) {
        perror("Listen failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("Epoll creation failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Register listening socket with epoll
    event.events = EPOLLIN;
    event.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) < 0) {
        perror("Epoll control failed");
        close(listen_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // Server's run-to-completion event loop
    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, continue loop
                continue;
            }
            perror("Epoll wait failed");
            break;
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listen_fd) {
                // Accept new connection
                int client_fd = accept(listen_fd, NULL, NULL);
                if (client_fd < 0) {
                    perror("Accept failed");
                    continue;
                }

                // Register new client socket with epoll
                event.events = EPOLLIN;
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
                    perror("Epoll control failed");
                    close(client_fd);
                    continue;
                }
                printf("Server: Accepted new connection\n");
            } else {
                // Handle client message
                char buf[MESSAGE_SIZE];
                int client_fd = events[i].data.fd;
                int bytes_read = recv(client_fd, buf, MESSAGE_SIZE, 0);
                if (bytes_read <= 0) {
                    // Close connection if error or client closed connection
                    close(client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    printf("Server: Closed connection\n");
                } else {
                    // Echo message back to client
                    send(client_fd, buf, bytes_read, 0);
                    printf("Server: Received and echoed message '%s'\n", buf);
                }
            }
        }
    }

    // Clean up
    close(listen_fd);
    close(epoll_fd);
    printf("Server stopped gracefully.\n");
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}
