all:
	g++ -O3 client.cpp -march=native -Wall -pedantic -std=c++17 -lSDL2 -lSDL2_gfx -lpthread -oclient
	g++ -O3 server.cpp -march=native -Wall -pedantic -std=c++17 -lpthread -oserver
client-debug:
	g++ -g -Og client.cpp -march=native -Wall -pedantic -std=c++17 -lSDL2 -lSDL2_gfx -lpthread -oclient
server-debug:
	g++ -g -Og server.cpp -march=native -Wall -pedantic -std=c++17 -lpthread -oserver