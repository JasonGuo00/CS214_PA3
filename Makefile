all: ttt.c
	rm -rf ttt
	gcc -Wall -Werror -g -fsanitize=address -pthread ttt.c -o ttt
	rm -rf ttts
	gcc -Wall -Werror -g -fsanitize=address -pthread ttts.c -o ttts

clean:
	rm -rf ttt
	rm -rf ttts