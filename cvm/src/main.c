#include "../include/ccbc.h"
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

static const char* resolve_pages_dir(void){
	struct stat st;
	if(stat("pages", &st) == 0 && S_ISDIR(st.st_mode)) return "pages";
	if(stat("../pages", &st) == 0 && S_ISDIR(st.st_mode)) return "../pages";
	return "pages";
}

int main(int argc, char** argv){
	if(argc < 2){
		fprintf(stderr, "usage:\n  cash run <file.ccbc> [entry_offset]\n  cash serve <file.ccbc|pages> [port]\n  cash dev (alias of serve pages)\n");
		return 2;
	}

	if(strcmp(argv[1], "dev") == 0){
		// alias: serve pages (auto-resolve pages dir)
		const char* pages_dir = resolve_pages_dir();
		uint8_t* blob=NULL; size_t blen=0; int port=3000;
		if(cc_build_bundle_from_pages(pages_dir, &blob, &blen)!=0){ fprintf(stderr, "build failed\n"); return 1; }
		FILE* tmp=fopen("/tmp/cash.bundle.ccbc","wb"); if(!tmp){ free(blob); return 1; }
		fwrite(blob,1,blen,tmp); fclose(tmp); free(blob);
		return run_http("/tmp/cash.bundle.ccbc", port);
	}

	if(strcmp(argv[1], "serve") == 0){
		if(argc == 3 && strcmp(argv[2], "pages") == 0){
			// build bundle in-memory from pages/ and serve
			const char* pages_dir = resolve_pages_dir();
			uint8_t* blob=NULL; size_t blen=0; int port=3000;
			if(cc_build_bundle_from_pages(pages_dir, &blob, &blen)!=0){ fprintf(stderr, "build failed\n"); return 1; }
			// write to temp file to reuse existing host; simple for MVP
			FILE* tmp=fopen("/tmp/cash.bundle.ccbc","wb"); if(!tmp){ free(blob); return 1; }
			fwrite(blob,1,blen,tmp); fclose(tmp); free(blob);
			return run_http("/tmp/cash.bundle.ccbc", port);
		} else {
			if(argc < 3){
				fprintf(stderr, "usage: cash serve <file.ccbc|pages> [port]\n");
				return 2;
			}
			const char* target = argv[2];
			int port = (argc >= 4) ? atoi(argv[3]) : 3000;
			if(strcmp(target, "pages") == 0){
				const char* pages_dir = resolve_pages_dir();
				uint8_t* blob=NULL; size_t blen=0;
				if(cc_build_bundle_from_pages(pages_dir, &blob, &blen)!=0){ fprintf(stderr, "build failed\n"); return 1; }
				FILE* tmp=fopen("/tmp/cash.bundle.ccbc","wb"); if(!tmp){ free(blob); return 1; }
				fwrite(blob,1,blen,tmp); fclose(tmp); free(blob);
				return run_http("/tmp/cash.bundle.ccbc", port);
			}
			return run_http(target, port);
		}
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


