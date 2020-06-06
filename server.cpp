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
    struct timeval *update_time;
    pthread_mutex_t *map_mutex;
    std::vector<std::vector<field_cells_type>> *map_s;
    int sockfd;
};

void *client_sender(void *data)
{
    thread_data &prop = *((thread_data *)data);

    struct timeval last_time;
    last_time.tv_sec = 0;
    last_time.tv_usec = 0;

    field_cells_type *buff = new field_cells_type[(*prop.map_s).size() * (*prop.map_s)[0].size()];

    int n = 1;
    while (n)
    {
        usleep(1);
        pthread_mutex_lock(prop.time_mutex);
        if ((*prop.update_time).tv_sec == last_time.tv_sec && (*prop.update_time).tv_usec == last_time.tv_usec)
        {
            pthread_mutex_unlock(prop.time_mutex);
            continue;
        }
        last_time = (*prop.update_time);
        pthread_mutex_unlock(prop.time_mutex);
        prepare_message_data_send prep_m;
        prep_m.type = field_type;
        pthread_mutex_lock(prop.map_mutex);
        prep_m.size = (*prop.map_s).size() * (*prop.map_s)[0].size() * sizeof(field_cells_type);
        n = send(prop.sockfd, (prepare_message_data_send *)&prep_m, sizeof(prepare_message_data_send), 0);
        for (size_t i = 0; i < (*prop.map_s).size(); i++)
            for (size_t j = 0; j < (*prop.map_s)[i].size(); j++)
                buff[i * (*prop.map_s).size() + j] = (*prop.map_s)[i][j];
        pthread_mutex_unlock(prop.map_mutex);
        n = send(prop.sockfd, (field_cells_type *)buff, prep_m.size, 0);
    }
    delete[] buff;
    return (void *)(0);
}

void *client_reciver(void *data)
{
    thread_data &prop = *((thread_data *)data);

    pthread_mutex_lock(prop.map_mutex);
    {
        bool flag_found = false;
        for (size_t i = 0; i < (*prop.map_s).size() && !flag_found; i++)
            for (size_t j = 0; j < (*prop.map_s)[i].size() && !flag_found; j++)
                if ((*prop.map_s)[i][j] == empty)
                {
                    (*prop.map_s)[i][j] = player;
                    flag_found = true;
                }
    }
    pthread_mutex_unlock(prop.map_mutex);
    pthread_mutex_lock(prop.time_mutex);
    gettimeofday(prop.update_time, NULL);
    pthread_mutex_unlock(prop.time_mutex);

    pthread_t sender_start;
    pthread_create(&sender_start, NULL, client_sender, (void *)&prop);

    action_send input_act;
    int n = recv(prop.sockfd, (action_send *)&input_act, sizeof(action_send), 0);
    while (n)
    {
        switch (input_act.action)
        {
        case move:
            pthread_mutex_lock(prop.map_mutex);
            if ((*prop.map_s)[input_act.to_x][input_act.to_y] == empty)
            {
                (*prop.map_s)[input_act.from_x][input_act.from_y] = empty;
                (*prop.map_s)[input_act.to_x][input_act.to_y] = player;
                pthread_mutex_lock(prop.time_mutex);
                gettimeofday(prop.update_time, NULL);
                pthread_mutex_unlock(prop.time_mutex);
            }
            pthread_mutex_unlock(prop.map_mutex);
            break;

        default:
            break;
        }
        n = recv(prop.sockfd, (action_send *)&input_act, sizeof(action_send), 0);
    }
    pthread_join(sender_start, (void **)&n);
    delete &prop;
    return (void *)(0);
}

void *main_client_thread(void *port)
{
    std::ifstream in("map.txt");
    int lines, colonums;
    in >> lines >> colonums;
    std::vector<std::vector<field_cells_type>> map_s(lines, std::vector<field_cells_type>(colonums));
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
    getsockname(sockfd, (struct sockaddr *)&servaddr, &servlen);
    std::cout << "Listening on port: " << ntohs(servaddr.sin_port) << std::endl;

    while (true)
    {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
        thread_data *prop = new thread_data;
        prop->sockfd = newsockfd;
        prop->map_mutex = &map_mutex;
        prop->map_s = &map_s;
        prop->time_mutex = &time_mutex;
        prop->update_time = &update_time;
        pthread_t client_start;
        pthread_create(&client_start, NULL, client_reciver, (void *)prop);
    }
    return (void *)(0);
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
    return 0;
}