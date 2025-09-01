#include "../include/ccbc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static int write_sock(const void* data, size_t len, void* user){
    int fd = *(int*)user;
    return send(fd, data, len, 0) == (ssize_t)len ? 0 : -1;
}

static int write_chunked(const void* data, size_t len, void* user){
    int fd = *(int*)user;
    char head[32]; int m = snprintf(head, sizeof(head), "%zx\r\n", len);
    if(send(fd, head, m, 0) != m) return -1;
    if(len>0 && send(fd, data, len, 0)!=(ssize_t)len) return -1;
    if(send(fd, "\r\n", 2, 0)!=2) return -1;
    return 0;
}

int run_http(const char* bundle_path, int port){
    // load bundle
    FILE* f = fopen(bundle_path, "rb");
    if(!f){ perror("open bundle"); return 1; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc(sz);
    if(!buf){ fclose(f); return 1; }
    if(fread(buf,1,sz,f)!=(size_t)sz){ fclose(f); free(buf); return 1; }
    fclose(f);
    cc_module_t mod; if(cc_load_module(buf, sz, &mod)!=0){ fprintf(stderr,"bad bundle\n"); free(buf); return 1; }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    int opt=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    struct sockaddr_in addr={0}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_ANY); addr.sin_port=htons((uint16_t)port);
    if(bind(s,(struct sockaddr*)&addr,sizeof(addr))<0){ perror("bind"); return 1; }
    listen(s, 16);
    printf("cashvm http listening on http://localhost:%d\n", port);
    for(;;){
        int c = accept(s, NULL, NULL);
        if(c<0) continue;
        char req[1024]; int n=recv(c, req, sizeof(req)-1, 0);
        if(n<=0){ close(c); continue; }
        req[n]=0;
        // parse path (very naive)
        char method[8]={0}, path[256]={0};
        sscanf(req, "%7s %255s", method, path);
        uint32_t entry=0;
        if(cc_find_route(&mod, path, &entry)!=0){
            const char* nf="HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot Found";
            send(c, nf, strlen(nf), 0); close(c); continue;
        }
        const char* hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nTransfer-Encoding: chunked\r\n\r\n";
        send(c, hdr, strlen(hdr), 0);
        cc_vm_t vm; cc_vm_init(&vm, &mod, entry);
        (void)write_sock; // silence unused
        cc_vm_run(&vm, write_chunked, &c);
        send(c, "0\r\n\r\n", 5, 0);
        close(c);
    }
    free(buf);
    return 0;
}


