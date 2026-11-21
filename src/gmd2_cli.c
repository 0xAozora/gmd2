#include "gmd2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

static int read_file(const char *path,uint8_t **out,size_t *n) {
    FILE *f=fopen(path,"rb"); if(!f){perror(path);return -1;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return -1;}
    long len=ftell(f); if(len<0||fseek(f,0,SEEK_SET)){fclose(f);return -1;}
    uint8_t *b=(uint8_t*)malloc(len? (size_t)len:1);
    if(!b){fclose(f);return -1;}
    if(fread(b,1,(size_t)len,f)!=(size_t)len){free(b);fclose(f);return -1;}
    fclose(f);*out=b;*n=(size_t)len;return 0;
}
static int write_file(const char *path,const uint8_t *data,size_t n) {
    FILE *f=fopen(path,"wb");if(!f){perror(path);return -1;}
    int ok=fwrite(data,1,n,f)==n&&fclose(f)==0;
    if(!ok){fprintf(stderr,"cannot write %s\n",path);return -1;}
    return 0;
}
int main(int argc,char **argv) {
    if(argc<5||argc>6||
       (strcmp(argv[1],"decode")&&strcmp(argv[1],"encode"))) {
        fprintf(stderr,"usage:\n  %s decode ARCHIVE.gmd2 LAYOUT.msgp OUTPUT.rgb\n"
                       "  %s encode INPUT.rgb LAYOUT.msgp OUTPUT.gmd2 [ZSTD_LEVEL]\n"
                       "  LAYOUT may also be msdf-atlas-gen JSON. RGB bytes are top-down.\n",
                       argv[0],argv[0]);return 2;
    }
    uint8_t *source=NULL,*metadata=NULL,*result=NULL;
    size_t source_len=0,meta_len=0,result_len=0;
    char error[256];Gmd2Metadata layout={0};int status=1;
    if(read_file(argv[2],&source,&source_len)||
       read_file(argv[3],&metadata,&meta_len))goto done;
    size_t first=0;
    while(first<meta_len && metadata[first] && strchr(" \t\r\n",metadata[first]))first++;
    int rc=first<meta_len && metadata[first]=='{'
        ? gmd2_layout_from_msdf_json((const char*)metadata,meta_len,&layout,error,sizeof(error))
        : gmd2_layout_from_msgpack(metadata,meta_len,&layout,error,sizeof(error));
    if(rc) {
        fprintf(stderr,"layout: %s\n",error);goto done;
    }
    if(!strcmp(argv[1],"decode"))
        rc=gmd2_archive_decode(source,source_len,&layout,&result,&result_len,error,sizeof(error));
    else {
        int level=argc==6?atoi(argv[5]):22;
        rc=gmd2_archive_encode(source,source_len,&layout,level,&result,&result_len,error,sizeof(error));
    }
    if(rc) {fprintf(stderr,"codec: %s\n",error);goto done;}
    if(write_file(argv[4],result,result_len))goto done;
    fprintf(stdout,"%s: %zu bytes -> %zu bytes\n",argv[1],source_len,result_len);
    status=0;
done:
    free(source);free(metadata);free(result);gmd2_free_layout(&layout);
    return status;
}
