FLAGS=-Wall -O2

build: ext2_fs.h analyst.c
	@ gcc $(FLAGS) analyst.c -o analyst
	@ cp interpreter_driver.sh interpreter
	@ chmod +x interpreter

clean:
	@ rm -f analyst interpreter
