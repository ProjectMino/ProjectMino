CC = g++
CFLAGS = -std=c++17 -g -O0 `sdl2-config --cflags`
LDFLAGS = `sdl2-config --libs` -lSDL2_ttf -lSDL2_image -lcurl


SRC = src/main.cpp src/game.cpp src/spins.cpp src/menu.cpp src/menu_modern.cpp src/wallpapers.cpp
OBJ = $(SRC:.cpp=.o)

all: tetris

tetris: $(OBJ)
	$(CC) $(CFLAGS) -o tetris $(OBJ) $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

src/menu.o: src/menu.cpp src/menu.h
	$(CC) $(CFLAGS) -c src/menu.cpp -o src/menu.o

clean:
	rm -f tetris $(OBJ)
