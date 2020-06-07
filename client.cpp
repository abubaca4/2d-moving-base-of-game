#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include "SDL2/SDL.h"

#include <vector>
#include <algorithm>

#include "common_types.hpp"

void DrawCircle(SDL_Renderer *renderer, int32_t centreX, int32_t centreY, int32_t radius) //какая-то функция отрисовки круга взята из интернета за неимением встроенной(на вики не нашёл)
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
            case player: //отрисовка круга для игрока
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, SDL_ALPHA_OPAQUE);
                DrawCircle(renderer, w_side_len * (j + 0.5), h_side_len * (i + 0.5), 0.8 * std::min(w / mat[i].size(), h / mat.size()) / 2);
                break;

            case wall: //отрисовка чёрного прямоугольника для стены
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

struct thread_data //структура для передачи информации в поток не объявленна в общем заголовочном файле так как может быть разная для сервера и клиента
{
    pthread_mutex_t *time_mutex;
    struct timeval *update_time;
    pthread_mutex_t *map_mutex;
    std::vector<std::vector<field_cells_type>> *map_s;
    int sockfd;
};

void *reciver(void *data)
{
    thread_data &prop = *((thread_data *)data); //приведение указателя к ссылке

    prepare_message_data_send input_format; //для получения сообщения ингициализирующего передачу
    int n = 1;

    while (n) //пока соединение не разорванно
    {
        n = recv(prop.sockfd, (prepare_message_data_send *)&input_format, sizeof(prepare_message_data_send), 0); //получение нового сообщения от сервера
        switch (input_format.type)                                                                               //в зависимости от типа передаваемых данных
        {
        case field_type:
            if ((*prop.map_s).size() != input_format.size || (*prop.map_s)[0].size() != input_format.second_size)
                *prop.map_s = std::vector<std::vector<field_cells_type>>(input_format.size, std::vector<field_cells_type>(input_format.second_size));
            pthread_mutex_lock(prop.map_mutex);
            for (size_t i = 0; i < (*prop.map_s).size(); i++) //получение поля построчно
                n = recv(prop.sockfd, (field_cells_type *)(*prop.map_s)[i].data(), (*prop.map_s)[i].size() * sizeof(field_cells_type), 0);
            pthread_mutex_unlock(prop.map_mutex);
            pthread_mutex_lock(prop.time_mutex);
            gettimeofday(prop.update_time, NULL); //обновление метки времени
            pthread_mutex_unlock(prop.time_mutex);
            break;

        default:
            break;
        }
    }

    return (void *)(0);
}

bool found_player(std::vector<std::vector<field_cells_type>> &mat, size_t &x, size_t &y) //поиск игрока на поле будет не актуален после введения списка игроков
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

    std::vector<std::vector<field_cells_type>> mat; //матрица для хранения поля будет пересоздана при получении размера
    struct timeval update_time, last_time;          //время обновления матрицы и время последней отрисованной марицы
    gettimeofday(&update_time, NULL);               //занесение текущего времени
    last_time = update_time;                        //для избегания отрисовки до получения версии сервера
    pthread_mutex_t time_mutex;                     //мьютекс для времени
    pthread_mutex_init(&time_mutex, NULL);
    pthread_mutex_t map_mutex; //мьютекс для карты
    pthread_mutex_init(&map_mutex, NULL);

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

    size_t x = 0; //координаты игрока
    size_t y = 0;

    thread_data share_data; //данные для передачи в поток
    share_data.sockfd = sockfd;
    share_data.map_s = &mat;
    share_data.map_mutex = &map_mutex;
    share_data.update_time = &update_time;
    share_data.time_mutex = &time_mutex;

    pthread_t reciver_thread; //запуск потока
    pthread_create(&reciver_thread, NULL, reciver, (void *)&share_data);

    pthread_mutex_lock(&time_mutex);
    while (!memcmp(&update_time, &last_time, sizeof(timeval))) //не даёт начать отрисовку пока не получена версия карты от сервера
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

        if (SDL_CreateWindowAndRenderer(640, 480, SDL_WINDOW_RESIZABLE, &window, &renderer) == 0) //создать окно размер которого разрешено изменять
        {
            SDL_bool done = SDL_FALSE; //хз зачем нужен такой тип в примере sdl он был)
            SDL_bool need_review = SDL_FALSE;

            while (!done && n) //пока не завершено или соединение не потеряно
            {
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
                if (!memcmp(&update_time, &last_time, sizeof(timeval)) && !need_review) //если карта не изменилась не отрисовывать снова, отрисовать снова если специальный флаг поднят
                {
                    pthread_mutex_unlock(&time_mutex);
                    usleep(1);
                    continue;
                }
                pthread_mutex_unlock(&time_mutex);
                last_time = update_time; //обновить до отрисованной версии
                need_review = SDL_FALSE; //опустить специальный флаг
                found_player(mat, x, y);

                SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
                SDL_RenderClear(renderer); //очистить поле белым цветом

                int w, h;
                SDL_GetWindowSize(window, &w, &h); //на случай изменения размеров окна получить новый размер

                pthread_mutex_lock(&map_mutex); //отрисовка элементов карты и границ между клетками
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
