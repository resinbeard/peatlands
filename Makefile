all:
	mkdir -p logs
	gcc -o peatlands peatlands.c -lpthread -lm -llo -lmonome

