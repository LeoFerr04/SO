CFLAGS:=-Wall -g
LDFLAGS:=-lpthread -pthread -lrt

all: cliente servidor

cliente: ./oFiles/cliente.o ./oFiles/util.o 
	$(CC) -g -o ./cliente ./oFiles/cliente.o ./oFiles/util.o $(LDFLAGS)

servidor: ./oFiles/servidor.o ./oFiles/util.o
	$(CC) -g -o ./servidor ./oFiles/servidor.o ./oFiles/util.o $(LDFLAGS)

./oFiles/cliente.o: ./cFiles/cliente.c
	$(CC) $(CFLAGS) -c ./cFiles/cliente.c -o ./oFiles/cliente.o

./oFiles/servidor.o: ./cFiles/servidor.c
	$(CC) $(CFLAGS) -c ./cFiles/servidor.c -o ./oFiles/servidor.o

./oFiles/util.o: ./cFiles/util.c
	$(CC) $(CFLAGS) -c ./cFiles/util.c -o ./oFiles/util.o

clean:
	rm -f ./oFiles/*.o ./cliente ./servidor ./logs/*