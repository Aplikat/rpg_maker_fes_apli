// FesLoader - instala mundos de RPG Maker Fes (game_NN.dat) en el extdata del juego.
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define BASE_URL "https://fes-test.muysimple001.workers.dev"
#define DEFAULT_EXTDATA_ID 0x1BD5u
#define MAX_WORLDS 64

typedef struct { char id[40]; char name[64]; u32 size; } World;
static World worlds[MAX_WORLDS];
static int nworlds = 0;
static u32 extdata_id = DEFAULT_EXTDATA_ID;
static char msg[512] = "";

static void say(const char *fmt, ...) {
    char line[200];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    strncat(msg, line, sizeof(msg) - strlen(msg) - 1);
    strncat(msg, "\n", sizeof(msg) - strlen(msg) - 1);
    printf("%s\n", line);
}

static Result http_get(const char *url, u8 **out, u32 *outsize) {
    httpcContext ctx;
    Result ret = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(ret)) { say("OpenContext fallo"); return ret; }
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "FesLoader/1.0");
    ret = httpcBeginRequest(&ctx);
    if (R_FAILED(ret)) { say("BeginRequest fallo (conexion segura?)"); httpcCloseContext(&ctx); return ret; }

    u32 status = 0;
    httpcGetResponseStatusCode(&ctx, &status);
    if (status != 200) { say("El servidor respondio HTTP %lu", status); httpcCloseContext(&ctx); return 0xE0000000u | status; }

    u32 cap = 0x4000, size = 0, got = 0;
    u8 *buf = malloc(cap);
    if (!buf) { httpcCloseContext(&ctx); return -1; }
    do {
        ret = httpcDownloadData(&ctx, buf + size, cap - size, &got);
        size += got;
        if (ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING && size == cap) {
            cap *= 2;
            u8 *nb = realloc(buf, cap);
            if (!nb) { free(buf); httpcCloseContext(&ctx); return -1; }
            buf = nb;
        }
    } while (ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);
    httpcCloseContext(&ctx);
    if (R_FAILED(ret)) { say("Descarga fallo"); free(buf); return ret; }
    *out = buf; *outsize = size;
    return 0;
}

static void load_config(void) {
    FILE *f = fopen("sdmc:/3ds/FesLoader/extdata_id.txt", "r");
    if (!f) return;
    unsigned v;
    if (fscanf(f, "%x", &v) == 1) extdata_id = v;
    fclose(f);
}

static Result open_extdata(FS_Archive *arc) {
    u32 path[3] = { MEDIATYPE_SD, extdata_id, 0 };
    FS_Path p = { PATH_BINARY, sizeof(path), path };
    return FSUSER_OpenArchive(arc, ARCHIVE_EXTDATA, p);
}

static int next_free_slot(FS_Archive arc) {
    bool used[100] = { 0 };
    Handle dir;
    if (R_FAILED(FSUSER_OpenDirectory(&dir, arc, fsMakePath(PATH_ASCII, "/")))) return -1;
    FS_DirectoryEntry ent[16];
    u32 n = 0;
    while (R_SUCCEEDED(FSDIR_Read(dir, &n, 16, ent)) &&
