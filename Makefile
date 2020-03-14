all: yashd yash

yash : yash.c
	gcc -g -Wall -o yash yash.c -lreadline
yashd : yashd.c
	gcc -g -Wall -o yashd yashd.c -lpthread
clean:
	rm *.o yashd yash
