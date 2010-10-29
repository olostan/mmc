#include <stdio.h>
#include <zlib.h>

int		main(int argc,char **argv) {
	int		ch;
	FILE	*fp;
	size_t		k,j,n,r;
	unsigned char	src[65536],dest[65536];
	size_t	sl;
	uLongf	ds;

	if (argc>2) {
		fp=fopen(argv[1],"rb");
		if (!fp)
			return 1;
	} else
		return 1;
	
	sl=0;
	n=fread(src+sl,1,65536-sl,fp);
	fclose(fp);
	ds=65536;
	r=compress(dest,&ds,src,n+sl);

	j=0;
	printf("{ %d,%d,\"%s\",\n",n+sl,(unsigned int)ds,argv[2]);
	while (j<ds) {
		k=0;
		putc('"',stdout);
		while (k<70 && j<ds) {
			++k;
			ch=(unsigned char)dest[j];
			if (ch!=' ' && !(ch>='a' && ch<='z') && !(ch>='A' && ch<='Z') && !(ch>='0' && ch<='9') && ch!='_') {
				printf("\\%03o",ch);
				k+=3;
			} else
				putc(ch,stdout);
			++j;
		}
		printf("\"\n");
	}
	printf("},\n");
	return 0;
}
