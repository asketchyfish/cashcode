#include "../include/ccbc.h"
#include "../include/version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// serve helper from http_host.c
int run_http(const char* bundle_path, int port);

static int write_stdout(const void* data, size_t len, void* user){
	(void)user;
	return fwrite(data, 1, len, stdout) == len ? 0 : -1;
}

static int is_dir(const char* path){ struct stat st; return (stat(path, &st) == 0) && S_ISDIR(st.st_mode); }

int main(int argc, char** argv){
	if(argc < 2){
		fprintf(stderr, "cash %s\nusage:\n  cash run <file.ccbc> [entry_offset]\n  cash serve <dir|file.ccbc> [port]\n  cash dev [dir] [port]\n", CASH_VERSION);
		return 2;
	}

	if(strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0){
		printf("%s\n", CASH_VERSION);
		return 0;
	}

	if(strcmp(argv[1], "dev") == 0){
		const char* dir = (argc >= 3) ? argv[2] : ".";
		int port = (argc >= 4) ? atoi(argv[3]) : 3000;
		if(!is_dir(dir)){ fprintf(stderr, "dev: '%s' is not a directory\n", dir); return 2; }
		uint8_t* blob=NULL; size_t blen=0;
		if(cc_build_bundle_from_pages(dir, &blob, &blen)!=0){ fprintf(stderr, "build failed\n"); return 1; }
		FILE* tmp=fopen("/tmp/cash.bundle.ccbc","wb"); if(!tmp){ free(blob); return 1; }
		fwrite(blob,1,blen,tmp); fclose(tmp); free(blob);
		return run_http("/tmp/cash.bundle.ccbc", port);
	}

	if(strcmp(argv[1], "serve") == 0){
		if(argc < 3){ fprintf(stderr, "usage: cash serve <dir|file.ccbc> [port]\n"); return 2; }
		const char* target = argv[2];
		int port = (argc >= 4) ? atoi(argv[3]) : 3000;
		if(is_dir(target)){
			uint8_t* blob=NULL; size_t blen=0;
			if(cc_build_bundle_from_pages(target, &blob, &blen)!=0){ fprintf(stderr, "build failed\n"); return 1; }
			FILE* tmp=fopen("/tmp/cash.bundle.ccbc","wb"); if(!tmp){ free(blob); return 1; }
			fwrite(blob,1,blen,tmp); fclose(tmp); free(blob);
			return run_http("/tmp/cash.bundle.ccbc", port);
		}
		return run_http(target, port);
	}

	if(strcmp(argv[1], "run") == 0){
		if(argc < 3){ fprintf(stderr, "usage: cash run <file.ccbc> [entry_offset]\n"); return 2; }
		const char* path = argv[2];
		uint32_t entry = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 10) : 0;
		FILE* f = fopen(path, "rb");
		if(!f){ perror("open"); return 1; }
		fseek(f, 0, SEEK_END);
		long sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		uint8_t* buf = (uint8_t*)malloc(sz);
		if(!buf){ fclose(f); return 1; }
		if(fread(buf,1,sz,f)!=(size_t)sz){ fclose(f); free(buf); return 1; }
		fclose(f);

		cc_module_t mod;
		if(cc_load_module(buf, sz, &mod)!=0){
			fprintf(stderr, "invalid module\n");
			free(buf);
			return 1;
		}
		cc_vm_t vm; cc_vm_init(&vm, &mod, entry);
		int rc = cc_vm_run(&vm, write_stdout, NULL);
		free(buf);
		if(rc!=0){ fprintf(stderr, "vm error %d\n", rc); return 1; }
		return 0;
	}

	fprintf(stderr, "unknown command\n");
	return 2;
}


