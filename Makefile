CXX := g++
CXXFLAGS := -Wall -g

SERVER_SRC := server.cpp
CLIENT_SRC := client.cpp
HEADERS := protocol.h

SERVER_BIN := server
CLIENT_BIN := client

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SERVER_SRC) -o $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN) -lglfw -lGL -lm -lopenal -lsndfile

.PHONY: clean
clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
