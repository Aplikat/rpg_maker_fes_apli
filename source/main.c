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
static char msg[768] = "";

static void say(const char *fmt, ...) {
    char line[200];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    strncat(msg, line, sizeof(msg) - strlen(msg) - 1);
    strncat(msg, "\n", sizeof(msg) - strlen(msg) - 1);
    printf("%s\n", line);
}

static Result http_get_once(const char *url, u8 **out, u32 *outsize) {
    httpcContext ctx;
    Result ret = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(ret)) { say("OpenContext: %08lX", ret); return ret; }
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "Mozilla/5.0 FesLoader/1.0");
    httpcAddRequestHeaderField(&ctx, "Accept", "*/*");
    ret = httpcBeginRequest(&ctx);
    if (R_FAILED(ret)) { say("BeginRequest: %08lX", ret); httpcCloseContext(&ctx); return ret; }

    u32 status = 0;
    ret = httpcGetResponseStatusCodeTimeout(&ctx, &status, 15000000000ULL);
    if (R_FAILED(ret)) { say("Estado: %08lX", ret); httpcCloseContext(&ctx); return ret; }
    if (status != 200) {
        say("HTTP %lu", status);
        httpcCloseContext(&ctx);
        return 0xE0000000u | (status & 0xFFFF);
    }

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
    if (R_FAILED(ret)) { say("Descarga: %08lX", ret); free(buf); return ret; }
    *out = buf; *outsize = size;
    return 0;
}

// Prueba HTTPS y, si falla, la misma direccion por HTTP simple.
static Result http_get(const char *url, u8 **out, u32 *outsize) {
    Result r = http_get_once(url, out, outsize);
    if (R_SUCCEEDED(r)) return r;
    if (strncmp(url, "https://", 8) == 0) {
        char alt[260];
        snprintf(alt, sizeof(alt), "http://%s", url + 8);
        say("Reintento por HTTP simple...");
        r = http_get_once(alt, out, outsize);
    }
    return r;
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
    while (R_SUCCEEDED(FSDIR_Read(dir, &n, 16, ent)) && n > 0) {
        for (u32 i = 0; i < n; i++) {
            char name[32] = { 0 };
            for (int k = 0; k < 31 && ent[i].name[k]; k++) name[k] = (char)ent[i].name[k];
            int num;
            if (sscanf(name, "game_%d.dat", &num) == 1 && num >= 0 && num < 100) used[num] = true;
        }
    }
    FSDIR_Close(dir);
    for (int i = 0; i < 100; i++) if (!used[i]) return i;
    return -1;
}

static void fetch_list(void) {
    msg[0] = 0;
    nworlds = 0;
    u32 wifi = 0;
    if (R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi == 0) { say("Sin Wi-Fi. Conectate y pulsa X."); return; }
    char url[200];
    snprintf(url, sizeof(url), "%s/api/list", BASE_URL);
    u8 *data; u32 size;
    Result r = http_get(url, &data, &size);
    if (R_FAILED(r)) { say("Error al bajar la lista: %08lX", r); return; }
    char *s = malloc(size + 1);
    memcpy(s, data, size); s[size] = 0; free(data);
    for (char *line = strtok(s, "\n"); line && nworlds < MAX_WORLDS; line = strtok(NULL, "\n")) {
        World *w = &worlds[nworlds];
        char *t1 = strchr(line, '\t'); if (!t1) continue; *t1++ = 0;
        char *t2 = strchr(t1, '\t');   if (!t2) continue; *t2++ = 0;
        snprintf(w->id, sizeof(w->id), "%s", line);
        snprintf(w->name, sizeof(w->name), "%s", t1);
        w->size = (u32)atoi(t2);
        nworlds++;
    }
    free(s);
    if (nworlds == 0) say("La lista llego vacia.");
}

static void install(const World *w) {
    msg[0] = 0;
    char url[200];
    snprintf(url, sizeof(url), "%s/api/world/%s", BASE_URL, w->id);
    say("Descargando %s...", w->name);
    u8 *data; u32 size;
    Result r = http_get(url, &data, &size);
    if (R_FAILED(r)) { say("Error de descarga: %08lX", r); return; }

    FS_Archive arc;
    r = open_extdata(&arc);
    if (R_FAILED(r)) {
        say("No pude abrir el extdata (ID %lX): %08lX", extdata_id, r);
        free(data); return;
    }
    int slot = next_free_slot(arc);
    if (slot < 0) { say("No hay ranura libre."); FSUSER_CloseArchive(arc); free(data); return; }

    char path[32];
    snprintf(path, sizeof(path), "/game_%02d.dat", slot);
    r = FSUSER_CreateFile(arc, fsMakePath(PATH_ASCII, path), 0, size);
    if (R_FAILED(r)) { say("No pude crear %s: %08lX", path, r); FSUSER_CloseArchive(arc); free(data); return; }
    Handle fh;
    r = FSUSER_OpenFile(&fh, arc, fsMakePath(PATH_ASCII, path), FS_OPEN_WRITE, 0);
    if (R_SUCCEEDED(r)) {
        u32 written = 0;
        r = FSFILE_Write(fh, &written, 0, data, size, FS_WRITE_FLUSH);
        FSFILE_Close(fh);
        if (R_SUCCEEDED(r) && written == size) say("Listo: instalado como %s", path);
        else say("Error al escribir: %08lX (%lu/%lu)", r, written, size);
    } else {
        say("No pude abrir %s: %08lX", path, r);
    }
    FSUSER_CloseArchive(arc);
    free(data);
}

static void draw(int sel) {
    consoleClear();
    printf("FesLoader - mundos de RPG Maker Fes\n");
    printf("Extdata ID: %lX\n", extdata_id);
    printf("Arriba/Abajo: elegir  A: instalar  X: recargar  START: salir\n\n");
    if (nworlds == 0) printf("(sin mundos)\n");
    for (int i = 0; i < nworlds; i++)
        printf("%s %s (%lu KB)\n", i == sel ? ">" : " ", worlds[i].name, worlds[i].size / 1024);
    if (msg[0]) printf("\n%s", msg);
}

int main(void) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    acInit();
    httpcInit(0x100000);
    fsInit();
    load_config();
    printf("Conectando...\n");
    fetch_list();

    int sel = 0;
    draw(sel);
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_START) break;
        if ((k & KEY_DOWN) && sel + 1 < nworlds) { sel++; draw(sel); }
        if ((k & KEY_UP) && sel > 0) { sel--; draw(sel); }
        if (k & KEY_X) { consoleClear(); printf("Recargando...\n"); fetch_list(); if (sel >= nworlds) sel = 0; draw(sel); }
        if ((k & KEY_A) && nworlds > 0) { install(&worlds[sel]); }
        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    fsExit(); httpcExit(); acExit(); gfxExit();
    return 0;
}
