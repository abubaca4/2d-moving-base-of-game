#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include "SDL2/SDL.h"

#include <vector>
#include <algorithm>

#include "common_types.hpp"

void DrawCircle(SDL_Renderer *renderer, int32_t centreX, int32_t centreY, int32_t radius)
{
    const int32_t diameter = (radius * 2);

    int32_t x = (radius - 1);
    int32_t y = 0;
    int32_t tx = 1;
    int32_t ty = 1;
    int32_t error = (tx - diameter);

    while (x >= y)
    {
        //  Each of the following renders an octant of the circle
        SDL_RenderDrawPoint(renderer, centreX + x, centreY - y);
        SDL_RenderDrawPoint(renderer, centreX + x, centreY + y);
        SDL_RenderDrawPoint(renderer, centreX - x, centreY - y);
        SDL_RenderDrawPoint(renderer, centreX - x, centreY + y);
        SDL_RenderDrawPoint(renderer, centreX + y, centreY - x);
        SDL_RenderDrawPoint(renderer, centreX + y, centreY + x);
        SDL_RenderDrawPoint(renderer, centreX - y, centreY - x);
        SDL_RenderDrawPoint(renderer, centreX - y, centreY + x);

        if (error <= 0)
        {
            ++y;
            error += ty;
            ty += 2;
        }

        if (error > 0)
        {
            --x;
            tx += 2;
            error += (tx - diameter);
        }
    }
}

void boards(SDL_Renderer *renderer, const int w, const int h, const int lines, const int colonums)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    int y_part = h / lines;
    for (int i = 0; i <= lines; i++)
        SDL_RenderDrawLine(renderer, 0, y_part * i, w, y_part * i);
    int x_part = w / colonums;
    for (int i = 0; i <= colonums; i++)
        SDL_RenderDrawLine(renderer, x_part * i, 0, x_part * i, h);
}

void map_s(SDL_Renderer *renderer, const int w, const int h, std::vector<std::vector<field_cells_type>> &mat)
{
    int h_side_len = h / mat.size();
    int w_side_len = w / mat[0].size();
    for (size_t i = 0; i < mat.size(); i++)
        for (size_t j = 0; j < mat[i].size(); j++)
            switch (mat[i][j])
            {
            case player:
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, SDL_ALPHA_OPAQUE);
                DrawCircle(renderer, w_side_len * (j + 0.5), h_side_len * (i + 0.5), 0.8 * std::min(w / mat[i].size(), h / mat.size()) / 2);
                break;

            case wall:
            {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
                SDL_Rect rect = {w_side_len * (int)j, h_side_len * (int)i, w_side_len, h_side_len};
                SDL_RenderFillRect(renderer, &rect);
            }
            break;
            default:
                break;
            }
}

struct thread_data
{
    pthread_mutex_t *time_mutex;
    struct timeval *update_time;
    pthread_mutex_t *map_mutex;
    std::vector<std::vector<field_cells_type>> *map_s;
    int sockfd;
};

void *reciver(void *data)
{
    thread_data &prop = *((thread_data *)data);

    int n = 1;
    prepare_message_data_send input_format;
    field_cells_type *buff = NULL;

    while (n)
    {
        n = recv(prop.sockfd, (prepare_message_data_send *)&input_format, sizeof(prepare_message_data_send), 0);
        switch (input_format.type)
        {
        case field_size:
            pthread_mutex_lock(prop.map_mutex);
            (*prop.map_s) = std::vector<std::vector<field_cells_type>>(input_format.size, std::vector<field_cells_type>(input_format.second_size));
            pthread_mutex_unlock(prop.map_mutex);
            break;

        case field_type:
            n = recv(prop.sockfd, (field_cells_type *)buff, input_format.size, 0);
            if (buff == NULL)
                buff = new field_cells_type[(*prop.map_s).size() * (*prop.map_s)[0].size()];
            pthread_mutex_lock(prop.map_mutex);
            for (size_t i = 0; i < (*prop.map_s).size(); i++)
                for (size_t j = 0; j < (*prop.map_s)[i].size(); j++)
                    (*prop.map_s)[i][j] = buff[i * (*prop.map_s).size() + j];
            pthread_mutex_unlock(prop.map_mutex);
            pthread_mutex_lock(prop.time_mutex);
            gettimeofday(prop.update_time, NULL);
            pthread_mutex_unlock(prop.time_mutex);
            break;

        default:
            break;
        }
    }

    delete[] buff;
    return (void *)(0);
}

bool found_player(std::vector<std::vector<field_cells_type>> &mat, size_t &x, size_t &y)
{
    for (size_t i = 0; i < mat.size(); i++)
        for (size_t j = 0; j < mat[i].size(); j++)
            if (mat[i][j] == player)
            {
                y = i;
                x = j;
                return true;
            }
    return false;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        perror("No address given");
        return -1;
    }

    int border = strstr(argv[1], ":") - argv[1];

    if (border + argv[1] == NULL)
    {
        perror("Not correct format of address");
        return -1;
    }

    char *ip = new char[border + 1];
    strncpy(ip, argv[1], border);
    ip[border] = '\0';
    int port = atoi(argv[1] + border + 1);

    std::vector<std::vector<field_cells_type>> mat;
    struct timeval update_time, last_time;
    gettimeofday(&update_time, NULL);
    last_time = update_time;
    pthread_mutex_t time_mutex;
    pthread_mutex_init(&time_mutex, NULL);
    pthread_mutex_t map_mutex;
    pthread_mutex_init(&map_mutex, NULL);

    struct sockaddr_in servaddr;
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_aton(ip, &(servaddr.sin_addr));

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        printf("Can't connect to server\n");
        return 1;
    }

    delete[] ip;
    int n = 1;

    size_t x = 0;
    size_t y = 0;

    thread_data share_data;
    share_data.sockfd = sockfd;
    share_data.map_s = &mat;
    share_data.map_mutex = &map_mutex;
    share_data.update_time = &update_time;
    share_data.time_mutex = &time_mutex;

    pthread_t reciver_thread;
    pthread_create(&reciver_thread, NULL, reciver, (void *)&share_data);

    pthread_mutex_lock(&time_mutex);
    while (!memcmp(&update_time, &last_time, sizeof(timeval)))
    {
        pthread_mutex_unlock(&time_mutex);
        usleep(1);
        pthread_mutex_lock(&time_mutex);
    }
    pthread_mutex_unlock(&time_mutex);

    if (SDL_Init(SDL_INIT_VIDEO) == 0)
    {
        SDL_Window *window = NULL;
        SDL_Renderer *renderer = NULL;

        if (SDL_CreateWindowAndRenderer(640, 480, SDL_WINDOW_RESIZABLE, &window, &renderer) == 0)
        {
            SDL_bool done = SDL_FALSE;

            while (!done && n)
            {
                SDL_Event event;

                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_QUIT)
                    {
                        done = SDL_TRUE;
                    }
                    else if (event.type == SDL_KEYDOWN)
                    {
                        struct action_send temp;
                        temp.action = move;
                        temp.from_x = x;
                        temp.from_y = y;

                        switch (event.key.keysym.sym)
                        {
                        case SDLK_UP:
                            temp.to_x = x;
                            temp.to_y = y - 1;
                            n = send(sockfd, (action_send *)&temp, sizeof(action_send), 0);
                            break;

                        case SDLK_DOWN:
                            temp.to_x = x;
                            temp.to_y = y + 1;
                            n = send(sockfd, (action_send *)&temp, sizeof(action_send), 0);
                            break;

                        case SDLK_LEFT:
                            temp.to_x = x - 1;
                            temp.to_y = y;
                            n = send(sockfd, (action_send *)&temp, sizeof(action_send), 0);
                            break;

                        case SDLK_RIGHT:
                            temp.to_x = x + 1;
                            temp.to_y = y;
                            n = send(sockfd, (action_send *)&temp, sizeof(action_send), 0);
                            break;
                        default:
                            break;
                        }
                    }
                }

                pthread_mutex_lock(&time_mutex);
                if (!memcmp(&update_time, &last_time, sizeof(timeval)))
                {
                    pthread_mutex_unlock(&time_mutex);
                    continue;
                }
                pthread_mutex_unlock(&time_mutex);
                last_time = update_time;
                found_player(mat, x, y);

                SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
                SDL_RenderClear(renderer);

                int w, h;
                SDL_GetWindowSize(window, &w, &h);

                pthread_mutex_lock(&map_mutex);
                boards(renderer, w, h, mat.size(), mat[0].size());
                map_s(renderer, w, h, mat);
                pthread_mutex_unlock(&map_mutex);

                SDL_RenderPresent(renderer);
            }
        }

        if (renderer)
        {
            SDL_DestroyRenderer(renderer);
        }
        if (window)
        {
            SDL_DestroyWindow(window);
        }
    }
    pthread_mutex_destroy(&time_mutex);
    pthread_mutex_destroy(&map_mutex);
    SDL_Quit();
    return 0;
}
