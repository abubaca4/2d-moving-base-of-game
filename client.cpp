#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>

#include "SDL2/SDL.h"
#include "SDL2/SDL2_gfxPrimitives.h"

#include <vector>
#include <algorithm>
#include <iostream>

#include "common_types.hpp"

void boards(SDL_Renderer *renderer, const int w, const int h, const int lines, const int colonums) //отрисовка границ клеток
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    int y_part = h / lines; //высота одной ячейки
    for (int i = 0; i <= lines; i++)
        SDL_RenderDrawLine(renderer, 0, y_part * i, w, y_part * i);
    int x_part = w / colonums; //ширина одной ячейки
    for (int i = 0; i <= colonums; i++)
        SDL_RenderDrawLine(renderer, x_part * i, 0, x_part * i, h);
}

void map_s(SDL_Renderer *renderer, const int w, const int h, std::vector<std::vector<field_cells_type>> &mat) //создание объектов поля, должно быть доработано до нескольких игроков разных цветов, текущие отображения мргут быть заменены на текстуры из файла
{
    int h_side_len = h / mat.size();    //высота одной ячейки
    int w_side_len = w / mat[0].size(); //ширина одной ячейки
    for (size_t i = 0; i < mat.size(); i++)
        for (size_t j = 0; j < mat[i].size(); j++)
            switch (mat[i][j])
            {
            case wall: //отрисовка чёрного прямоугольника для стены
                boxRGBA(renderer, w_side_len * (int)j, h_side_len * (int)i, w_side_len * (int)(j + 1), h_side_len * (int)(i + 1), 0, 0, 0, SDL_ALPHA_OPAQUE);
                break;

            case door_lock:
                boxRGBA(renderer, w_side_len * (int)j, h_side_len * (int)i, w_side_len * (int)(j + 1), h_side_len * (int)(i + 1), 0, 0, 0, SDL_ALPHA_OPAQUE);
                circleRGBA(renderer, w_side_len * (j + 0.5), h_side_len * (i + 0.5), std::min(w_side_len, h_side_len) / 2, 255, 0, 0, SDL_ALPHA_OPAQUE);
                break;

            case door_open:
                boxRGBA(renderer, w_side_len * (int)j, h_side_len * (int)i, w_side_len * (int)(j + 1), h_side_len * (int)(i + 1), 0, 0, 0, SDL_ALPHA_OPAQUE);
                filledCircleRGBA(renderer, w_side_len * (j + 0.5), h_side_len * (i + 0.5), std::min(w_side_len, h_side_len) / 2, 255, 255, 255, SDL_ALPHA_OPAQUE);
                circleRGBA(renderer, w_side_len * (j + 0.5), h_side_len * (i + 0.5), std::min(w_side_len, h_side_len) / 2, 255, 0, 0, SDL_ALPHA_OPAQUE);
                break;

            case trap_on:
                lineRGBA(renderer, w_side_len * (int)j, h_side_len * (int)i, w_side_len * (int)(j + 1), h_side_len * (int)(i + 1), 255, 0, 0, SDL_ALPHA_OPAQUE);
                lineRGBA(renderer, w_side_len * (int)j, h_side_len * (int)(i + 1), w_side_len * (int)(j + 1), h_side_len * (int)i, 255, 0, 0, SDL_ALPHA_OPAQUE);
                break;

            case coin:
                filledCircleRGBA(renderer, w_side_len * (j + 0.5), h_side_len * (i + 0.5), std::min(w_side_len, h_side_len) / 4, 255, 215, 0, SDL_ALPHA_OPAQUE);
                filledCircleRGBA(renderer, w_side_len * (j + 0.5), h_side_len * (i + 0.5), std::min(w_side_len, h_side_len) / 7, 255, 255, 255, SDL_ALPHA_OPAQUE);
                break;

            default:
                break;
            }
}

void players_print(SDL_Renderer *renderer, const int w, const int h, const size_t x_size, const size_t y_size, std::vector<player> &player_list)
{
    int h_side_len = h / x_size; //высота одной ячейки
    int w_side_len = w / y_size; //ширина одной ячейки
    for (size_t i = 0; i < player_list.size(); i++)
        if (player_list[i].is_alive)
        {
            filledCircleRGBA(renderer, w_side_len * (player_list[i].x + 0.5), h_side_len * (player_list[i].y + 0.5), 0.8 * std::min(w_side_len, h_side_len) / 2, player_list[i].r, player_list[i].g, player_list[i].b, SDL_ALPHA_OPAQUE);
        }
}

struct thread_data //структура для передачи информации в поток не объявленна в общем заголовочном файле так как может быть разная для сервера и клиента
{
    pthread_mutex_t *time_mutex;
    struct timeval *update_time;
    pthread_mutex_t *map_mutex;
    std::vector<std::vector<field_cells_type>> *map_s;
    pthread_mutex_t *player_mutex;
    std::vector<player> *player_list;
    int sockfd;
    size_t my_id;
};

void *reciver(void *data)
{
    thread_data &prop = *((thread_data *)data); //приведение указателя к ссылке

    prepare_message_data_send input_format; //для получения сообщения ингициализирующего передачу
    int n = recv(prop.sockfd, (prepare_message_data_send *)&input_format, sizeof(prepare_message_data_send), 0);

    while (n) //пока соединение не разорванно
    {
        switch (input_format.type) //в зависимости от типа передаваемых данных
        {
        case field_type:
            pthread_mutex_lock(prop.map_mutex);
            if (prop.map_s->size() != input_format.size || prop.map_s->at(0).size() != input_format.second_size)
                *prop.map_s = std::vector<std::vector<field_cells_type>>(input_format.size, std::vector<field_cells_type>(input_format.second_size));
            for (size_t i = 0; i < prop.map_s->size(); i++) //получение поля построчно
                n = recv(prop.sockfd, (field_cells_type *)prop.map_s->at(i).data(), prop.map_s->at(i).size() * sizeof(field_cells_type), 0);
            pthread_mutex_unlock(prop.map_mutex);
            pthread_mutex_lock(prop.time_mutex);
            gettimeofday(prop.update_time, NULL); //обновление метки времени
            pthread_mutex_unlock(prop.time_mutex);
            break;
        case player_list:
            pthread_mutex_lock(prop.player_mutex);
            if (prop.player_list->size() != input_format.size)
                *prop.player_list = std::vector<player>(input_format.size);
            n = recv(prop.sockfd, (player *)prop.player_list->data(), prop.player_list->size() * sizeof(player), 0);
            pthread_mutex_unlock(prop.player_mutex);
            pthread_mutex_lock(prop.time_mutex);
            gettimeofday(prop.update_time, NULL); //обновление метки времени
            pthread_mutex_unlock(prop.time_mutex);
            break;

        case my_number_from_list:
            prop.my_id = input_format.size;
            break;

        default:
            break;
        }
        n = recv(prop.sockfd, (prepare_message_data_send *)&input_format, sizeof(prepare_message_data_send), 0); //получение нового сообщения от сервера
    }

    return (void *)(0);
}

int main(int argc, char *argv[])
{
    if (argc != 2) //дан ли аргумент адрес
    {
        perror("No address given");
        return -1;
    }

    int border = strstr(argv[1], ":") - argv[1]; //поиск : разделителя строки

    if (border + argv[1] == NULL) //strstr возвращает NULL если ничего не найдено
    {
        perror("Not correct format of address");
        return -1;
    }

    char *ip = new char[border + 1];       //для хранения ip адреса вырезанного из строки
    strncpy(ip, argv[1], border);          //копирование ip адреса
    ip[border] = '\0';                     //окончание строки
    int port = atoi(argv[1] + border + 1); // преобразование порта к числовому виду

    if (port == 0) // 0 если строка не число
    {
        perror("Not correct port");
        return -1;
    }

    std::vector<player> player_list;                //список игроков
    std::vector<std::vector<field_cells_type>> mat; //матрица для хранения поля будет пересоздана при получении размера
    struct timeval update_time, last_time;          //время обновления данных и время последних отрисованных данных
    gettimeofday(&update_time, NULL);               //занесение текущего времени
    last_time = update_time;                        //для избегания отрисовки до получения версии сервера
    pthread_mutex_t time_mutex;                     //мьютекс для времени
    pthread_mutex_init(&time_mutex, NULL);
    pthread_mutex_t map_mutex; //мьютекс для карты
    pthread_mutex_init(&map_mutex, NULL);
    pthread_mutex_t player_mutex; //мьютекс для списка игроков
    pthread_mutex_init(&player_mutex, NULL);

    struct sockaddr_in servaddr;
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_aton(ip, &(servaddr.sin_addr));
    delete[] ip; //использован больше не нужен

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        printf("Can't connect to server\n");
        return 1;
    }

    int n = 1; // для хранения возврата соединения, если 0 соединение с сервером потеряно

    thread_data share_data; //данные для передачи в поток
    share_data.sockfd = sockfd;
    share_data.map_s = &mat;
    share_data.map_mutex = &map_mutex;
    share_data.update_time = &update_time;
    share_data.time_mutex = &time_mutex;
    share_data.player_mutex = &player_mutex;
    share_data.player_list = &player_list;

    pthread_t reciver_thread; //запуск потока
    pthread_create(&reciver_thread, NULL, reciver, (void *)&share_data);

    pthread_mutex_lock(&map_mutex);
    pthread_mutex_lock(&player_mutex);
    while (mat.empty() || player_list.empty()) //не даёт начать отрисовку пока не получена карта и список игроков от сервера
    {
        pthread_mutex_unlock(&map_mutex);
        pthread_mutex_unlock(&player_mutex);
        usleep(1);
        if (pthread_kill(reciver_thread, 0) != 0) //проверка жив ли поток(поток слушающий данные узнаёт о потере соединения первый и закрывается)
        {
            std::cout << "connection lost" << std::endl;
            close(sockfd);
            exit(0);
        }
        pthread_mutex_lock(&map_mutex);
        pthread_mutex_lock(&player_mutex);
    }
    pthread_mutex_unlock(&map_mutex);
    pthread_mutex_unlock(&player_mutex);

    if (SDL_Init(SDL_INIT_VIDEO) == 0)
    {
        SDL_Window *window = NULL;
        SDL_Renderer *renderer = NULL;

        if (SDL_CreateWindowAndRenderer(640, 480, SDL_WINDOW_RESIZABLE, &window, &renderer) == 0) //создать окно размер которого разрешено изменять
        {
            SDL_bool done = SDL_FALSE; //хз зачем нужен такой тип в примере sdl он был)
            SDL_bool need_review = SDL_FALSE;

            while (!done && n) //пока не завершено или соединение не потеряно
            {
                if (pthread_kill(reciver_thread, 0) != 0) //проверка жив ли поток(поток слушающий данные узнаёт о потере соединения первый и закрывается)
                {
                    n = 0;
                    std::cout << "connection lost" << std::endl;
                    continue;
                }

                SDL_Event event;

                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_QUIT)
                    {
                        done = SDL_TRUE;
                    }
                    else if (event.type == SDL_WINDOWEVENT)
                    {
                        switch (event.window.event)
                        {
                        case SDL_WINDOWEVENT_RESTORED:
                        case SDL_WINDOWEVENT_SIZE_CHANGED: //для нормальной отрисовки при resize
                            need_review = SDL_TRUE;
                            break;

                        default:
                            break;
                        }
                    }
                    else if (event.type == SDL_KEYDOWN)
                    {
                        struct action_send temp; //отправка действий на сервер
                        pthread_mutex_lock(&player_mutex);
                        size_t x = player_list[share_data.my_id].x;
                        size_t y = player_list[share_data.my_id].y;
                        pthread_mutex_unlock(&player_mutex);
                        temp.from_x = x;
                        temp.from_y = y;

                        switch (event.key.keysym.sym)
                        {
                        case SDLK_UP:
                            temp.to_x = x;
                            temp.to_y = y - 1;
                            temp.action = move;
                            n = send(sockfd, (action_send *)&temp, sizeof(action_send), MSG_NOSIGNAL);
                            break;

                        case SDLK_DOWN:
                            temp.to_x = x;
                            temp.to_y = y + 1;
                            temp.action = move;
                            n = send(sockfd, (action_send *)&temp, sizeof(action_send), MSG_NOSIGNAL);
                            break;

                        case SDLK_LEFT:
                            temp.to_x = x - 1;
                            temp.to_y = y;
                            temp.action = move;
                            n = send(sockfd, (action_send *)&temp, sizeof(action_send), MSG_NOSIGNAL);
                            break;

                        case SDLK_RIGHT:
                            temp.to_x = x + 1;
                            temp.to_y = y;
                            temp.action = move;
                            n = send(sockfd, (action_send *)&temp, sizeof(action_send), MSG_NOSIGNAL);
                            break;

                        case SDLK_r:
                            temp.action = doorAction;
                            pthread_mutex_lock(&map_mutex);
                            if (y - 1 < mat.size() && (mat[y - 1][x] == door_open || mat[y - 1][x] == door_lock))
                            {
                                temp.to_x = x;
                                temp.to_y = y - 1;
                                n = send(sockfd, (action_send *)&temp, sizeof(action_send), MSG_NOSIGNAL);
                            }
                            else if (y + 1 < mat.size() && (mat[y + 1][x] == door_open || mat[y + 1][x] == door_lock))
                            {
                                temp.to_x = x;
                                temp.to_y = y + 1;
                                n = send(sockfd, (action_send *)&temp, sizeof(action_send), MSG_NOSIGNAL);
                            }
                            else if (x - 1 < mat[y].size() && (mat[y][x - 1] == door_open || mat[y][x - 1] == door_lock))
                            {
                                temp.to_x = x - 1;
                                temp.to_y = y;
                                n = send(sockfd, (action_send *)&temp, sizeof(action_send), MSG_NOSIGNAL);
                            }
                            else if (x + 1 < mat[y].size() && (mat[y][x + 1] == door_open || mat[y][x + 1] == door_lock))
                            {
                                temp.to_x = x + 1;
                                temp.to_y = y;
                                n = send(sockfd, (action_send *)&temp, sizeof(action_send), MSG_NOSIGNAL);
                            }
                            pthread_mutex_unlock(&map_mutex);
                            break;

                        default:
                            break;
                        }
                    }
                }

                pthread_mutex_lock(&time_mutex);
                if (!memcmp(&update_time, &last_time, sizeof(timeval)) && !need_review) //если карта не изменилась не отрисовывать снова, отрисовать снова если специальный флаг поднят
                {
                    pthread_mutex_unlock(&time_mutex);
                    usleep(1);
                    continue;
                }
                pthread_mutex_unlock(&time_mutex);
                last_time = update_time; //обновить до отрисованной версии
                need_review = SDL_FALSE; //опустить специальный флаг

                SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
                SDL_RenderClear(renderer); //очистить поле белым цветом

                int w, h;
                SDL_GetWindowSize(window, &w, &h); //на случай изменения размеров окна получить новый размер

                pthread_mutex_lock(&map_mutex); //отрисовка элементов карты и границ между клетками
                boards(renderer, w, h, mat.size(), mat[0].size());
                map_s(renderer, w, h, mat);
                pthread_mutex_unlock(&map_mutex);
                pthread_mutex_lock(&player_mutex);
                players_print(renderer, w, h, mat.size(), mat[0].size(), player_list);
                pthread_mutex_unlock(&player_mutex);

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
    pthread_mutex_destroy(&player_mutex);
    SDL_Quit();
    close(sockfd);
    return 0;
}
