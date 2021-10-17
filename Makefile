all:
	g++ server.cpp -o server -lpthread
	g++ clients.cpp -o client -lpthread

clean:
	rm -f ./client ./server