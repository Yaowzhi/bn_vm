#include<stdio.h>
#define LENGTH 100

main()
{
	FILE *fd;
	char str[LENGTH];

	fd=fopen("hello2.txt","w+");
	if(fd)
	{
		fputs("Hello world!",fd);
		fclose(fd);
	}

	fd=fopen("hello2.txt","r");
	fgets(str,LENGTH,fd);
	printf("%s\n",str);
	fclose(fd);

}
