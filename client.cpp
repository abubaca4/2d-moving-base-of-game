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

void map_s(SDL_Renderer *renderer, const int w, const int h, std::vector<std::vector<uint8_t>> &mat)
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

int main(int argc, char *argv[])
{
    const int lines = 8;
    const int colonums = 8;
    std::vector<std::vector<uint8_t>> mat(lines, std::vector<uint8_t>(colonums, empty));
    size_t x = 0;
    size_t y = 0;
    mat[x][y] = player;
    mat[1][1] = wall;
    if (SDL_Init(SDL_INIT_VIDEO) == 0)
    {
        SDL_Window *window = NULL;
        SDL_Renderer *renderer = NULL;

        if (SDL_CreateWindowAndRenderer(640, 480, SDL_WINDOW_RESIZABLE, &window, &renderer) == 0)
        {
            SDL_bool done = SDL_FALSE;

            while (!done)
            {
                SDL_Event event;

                SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
                SDL_RenderClear(renderer);

                int w, h;
                SDL_GetWindowSize(window, &w, &h);

                boards(renderer, w, h, lines, colonums);
                map_s(renderer, w, h, mat);

                SDL_RenderPresent(renderer);

                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_QUIT)
                    {
                        done = SDL_TRUE;
                    }
                    else if (event.type == SDL_KEYDOWN)
                    {
                        switch (event.key.keysym.sym)
                        {
                        case SDLK_UP:
                            if (y > 0 && mat[y - 1][x] == empty)
                            {
                                mat[y][x] = empty;
                                y--;
                                mat[y][x] = player;
                            }
                            break;

                        case SDLK_DOWN:
                            if (y + 1 < mat.size() && mat[y + 1][x] == empty)
                            {
                                mat[y][x] = empty;
                                y++;
                                mat[y][x] = player;
                            }
                            break;

                        case SDLK_LEFT:
                            if (x > 0 && mat[y][x - 1] == empty)
                            {
                                mat[y][x] = empty;
                                x--;
                                mat[y][x] = player;
                            }
                            break;

                        case SDLK_RIGHT:
                            if (x + 1 < mat[y].size() && mat[y][x + 1] == empty)
                            {
                                mat[y][x] = empty;
                                x++;
                                mat[y][x] = player;
                            }
                            break;
                        }
                    }
                }
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
    SDL_Quit();
    return 0;
}
