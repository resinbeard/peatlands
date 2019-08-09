osc:
	gcc  -o cyperus cyperus.c rtqueue.c libcyperus.c dsp.c -ljack -lpthread -lm -llo -lfftw3_threads -lfftw3 -lmonome
test:
	gcc -o cyperus main.c rtqueue.c libcyperus.c -ljack -lpthread -lm

