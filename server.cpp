#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>
#include <fstream>

#include "common_types.hpp"

struct thread_data
{
    pthread_mutex_t *time_mutex;
    struct timeval* update_time;
    pthread_mutex_t *map_mutex;
    std::vector<std::vector<uint8_t>> *map_s;
    int sockfd;
    struct sockaddr_in cliaddr;
};

void *client_sender(void *data)
{
    thread_data prop = *((thread_data *)data);

    int n = 1;
    usleep(1);
}

void *client_reciver(void *data)
{
    thread_data prop = *((thread_data *)data);
    delete (thread_data *)data;
    client_sender((void *)&prop);
    pthread_t sender_start;
    pthread_create(&sender_start, NULL, client_sender, (void *)&prop);

    int n = 1;
    while (n)
    {
        /* code */
    }
}

void *main_client_thread(void *port)
{
    std::ifstream in("map.txt");
    int lines, colonums;
    in >> lines >> colonums;
    std::vector<std::vector<uint8_t>> map_s(lines, std::vector<uint8_t>(colonums));
    if (!in.is_open())
    {
        std::cout << "Map file not found\n";
        exit(2);
    }
    for (size_t i = 0; i < map_s.size(); i++)
        for (size_t j = 0; j < map_s[i].size(); j++)
            in >> map_s[i][j];

    in.close(); 

    struct timeval update_time;
    gettimeofday(&update_time, NULL);
    pthread_mutex_t time_mutex;
    pthread_mutex_init(&time_mutex, NULL);

    pthread_mutex_t map_mutex;
    pthread_mutex_init(&map_mutex, NULL);

    int sockfd;
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(*((int *)port));
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        servaddr.sin_port = 0;
        if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        {
            close(sockfd);
            exit(1);
        }
    }

    socklen_t servlen = sizeof(servaddr);
    listen(sockfd, 100);
    std::cout << "Listening on port: %d\n", ntohs(servaddr.sin_port);
    
    while (true)
    {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
        thread_data *prop = new thread_data;
        prop->cliaddr = cliaddr;
        prop->sockfd = sockfd;
        prop->map_mutex = &map_mutex;
        prop->map_s = &map_s;
        prop->time_mutex = &time_mutex;
        prop->update_time = &update_time;
        pthread_t client_start;
        pthread_create(&client_start, NULL, client_reciver, (void *)prop);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cout << "No port given\n";
        return -1;
    }

    int port = atoi(argv[1]);

    if (port == 0)
    {
        perror("Not correct port");
        return -1;
    }

    pthread_t main_thread;

    pthread_create(&main_thread, NULL, main_client_thread, (void *)&port);

    std::cout << "Ready to recive commands\n";
    bool done = false;
    std::string command;
    while (!done)
    {
        std::getline(std::cin, command);
        if (command == "exit")
        {
            pthread_cancel(main_thread);
            exit(0);
        }
        else if (command == "help")
        {
            std::cout << "Avalible commands:\n";
            std::cout << "exit - closes app\n";
            std::cout << "help - shows avalible commands\n";
        }
        else
        {
            std::cout << "Unknown command, please use help to get list of avalible commands\n";
        }
    }
}