FLAGS=-Wall -O2

build: ext2_fs.h analyst.c
	@ gcc $(FLAGS) analyst.c -o analyst

clean:
	@ rm -f analyst
