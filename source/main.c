// FesLoader - baja e instala mundos de RPG Maker Fes (game_NN.dat) y sube los tuyos.
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define BASE_URL "http://fes-test.muysimple001.workers.dev"
#define DEFAULT_EXTDATA_ID 0x1BD5u
#define MAX_WORLDS 300
#define VISIBLE 14

typedef struct { char id[40]; char name[64]; u32 size; } World;
typedef struct { int slot; u32 size; } Local;

static World worlds[MAX_WORLDS];
static int nworlds = 0;
static Local locals[100];
static int nlocals = 0;
static u32 extdata_id = DEFAULT_EXTDATA_ID;
static char msg[768] = "";
static int mode = 0;  // 0 = bajar del catalogo, 1 = subir los mios

static void say(const char *fmt, ...) {
    char line[200];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    strncat(msg, line, sizeof(msg) - strlen(msg) - 1);
    strncat(msg, "\n", sizeof(msg) - strlen(msg) - 1);
}

// Hace una peticion (GET o POST). Devuelve 0 si hubo respuesta, con su codigo en *status.
static Result http_request(HTTPC_RequestMethod method, const char *url, const u8 *body, u32 bodysize,
                           u32 *status, u8 **out, u32 *outsize) {
    httpcContext ctx;
    Result ret = httpcOpenContext(&ctx, method, url, 1);
    if (R_FAILED(ret)) { say("OpenContext: %08lX", ret); return ret; }
    httpcAddRequestHeaderField(&ctx, "User-Agent", "Mozilla/5.0 FesLoader/1.0");
    httpcAddRequestHeaderField(&ctx, "Accept", "*/*");
    if (body) {
        httpcAddRequestHeaderField(&ctx, "Content-Type", "application/octet-stream");
        ret = httpcAddPostDataRaw(&ctx, (const u32 *)body, bodysize);
        if (R_FAILED(ret)) { say("PostData: %08lX", ret); httpcCloseContext(&ctx); return ret; }
    }
    ret = httpcBeginRequest(&ctx);
    if (R_FAILED(ret)) { say("BeginRequest: %08lX", ret); httpcCloseContext(&ctx); return ret; }
    *status = 0;
    ret = httpcGetResponseStatusCodeTimeout(&ctx, status, 20000000000ULL);
    if (R_FAILED(ret)) { say("Estado: %08lX", ret); httpcCloseContext(&ctx); return ret; }

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
    u8 *nb = realloc(buf, size + 1);  // espacio para un 0 final (texto)
    if (!nb) { free(buf); return -1; }
    nb[size] = 0;
    *out = nb; *outsize = size;
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

// Lee la lista de game_NN.dat que hay en el extdata (rellena locals[]).
static void scan_local(FS_Archive arc) {
    nlocals = 0;
    Handle dir;
    if (R_FAILED(FSUSER_OpenDirectory(&dir, arc, fsMakePath(PATH_ASCII, "/")))) return;
    FS_DirectoryEntry ent[16];
    u32 n = 0;
    while (R_SUCCEEDED(FSDIR_Read(dir, &n, 16, ent)) && n > 0) {
        for (u32 i = 0; i < n; i++) {
            char name[32] = { 0 };
            for (int k = 0; k < 31 && ent[i].name[k]; k++) name[k] = (char)ent[i].name[k];
            int num;
            if (sscanf(name, "game_%d.dat", &num) == 1 && num >= 0 && num < 100 && nlocals < 100) {
                locals[nlocals].slot = num;
                locals[nlocals].size = (u32)ent[i].fileSize;
                nlocals++;
            }
        }
    }
    FSDIR_Close(dir);
}

static void refresh_local(void) {
    FS_Archive arc;
    Result r = open_extdata(&arc);
    if (R_FAILED(r)) { say("No pude abrir el extdata (ID %lX): %08lX", extdata_id, r); nlocals = 0; return; }
    scan_local(arc);
    FSUSER_CloseArchive(arc);
}

static int next_free_slot(void) {
    bool used[100] = { 0 };
    for (int i = 0; i < nlocals; i++) used[locals[i].slot] = true;
    for (int i = 0; i < 100; i++) if (!used[i]) return i;
    return -1;
}

static void fetch_list(void) {
    nworlds = 0;
    u32 wifi = 0;
    if (R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi == 0) { say("Sin Wi-Fi. Conectate y pulsa X."); return; }
    char url[200];
    snprintf(url, sizeof(url), "%s/api/list", BASE_URL);
    u8 *data; u32 size, status;
    Result r = http_request(HTTPC_METHOD_GET, url, NULL, 0, &status, &data, &size);
    if (R_FAILED(r)) { say("Error al bajar la lista: %08lX", r); return; }
    if (status != 200) { say("La lista respondio HTTP %lu", status); free(data); return; }
    for (char *line = strtok((char *)data, "\n"); line && nworlds < MAX_WORLDS; line = strtok(NULL, "\n")) {
        World *w = &worlds[nworlds];
        char *t1 = strchr(line, '\t'); if (!t1) continue; *t1++ = 0;
        char *t2 = strchr(t1, '\t');   if (!t2) continue; *t2++ = 0;
        snprintf(w->id, sizeof(w->id), "%s", line);
        snprintf(w->name, sizeof(w->name), "%s", t1);
        w->size = (u32)atoi(t2);
        nworlds++;
    }
    free(data);
    if (nworlds == 0) say("El catalogo esta vacio.");
}

static void install(const World *w) {
    msg[0] = 0;
    char url[200];
    snprintf(url, sizeof(url), "%s/api/world/%s", BASE_URL, w->id);
    say("Descargando %s...", w->name);
    u8 *data; u32 size, status;
    Result r = http_request(HTTPC_METHOD_GET, url, NULL, 0, &status, &data, &size);
    if (R_FAILED(r)) { say("Error de descarga: %08lX", r); return; }
    if (status != 200) { say("El servidor respondio HTTP %lu", status); free(data); return; }

    FS_Archive arc;
    r = open_extdata(&arc);
    if (R_FAILED(r)) { say("No pude abrir el extdata (ID %lX): %08lX", extdata_id, r); free(data); return; }
    scan_local(arc);
    int slot = next_free_slot();
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

static void upload(const Local *l) {
    msg[0] = 0;
    FS_Archive arc;
    Result r = open_extdata(&arc);
    if (R_FAILED(r)) { say("No pude abrir el extdata (ID %lX): %08lX", extdata_id, r); return; }
    char path[32];
    snprintf(path, sizeof(path), "/game_%02d.dat", l->slot);
    Handle fh;
    r = FSUSER_OpenFile(&fh, arc, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
    if (R_FAILED(r)) { say("No pude abrir %s: %08lX", path, r); FSUSER_CloseArchive(arc); return; }
    u64 fsz = 0;
    FSFILE_GetSize(fh, &fsz);
    u8 *buf = malloc((size_t)fsz);
    u32 rd = 0;
    if (buf) r = FSFILE_Read(fh, &rd, 0, buf, (u32)fsz);
    FSFILE_Close(fh);
    FSUSER_CloseArchive(arc);
    if (!buf || R_FAILED(r) || rd != (u32)fsz) { say("No pude leer %s", path); free(buf); return; }

    say("Subiendo %s...", path);
    char url[200];
    snprintf(url, sizeof(url), "%s/api/upload", BASE_URL);
    u8 *resp; u32 rsize, status;
    r = http_request(HTTPC_METHOD_POST, url, buf, rd, &status, &resp, &rsize);
    free(buf);
    if (R_FAILED(r)) { say("Error al subir: %08lX", r); return; }
    for (u32 i = 0; i < rsize; i++) if (resp[i] == '\n') resp[i] = ' ';
    say("Servidor (%lu): %s", status, resp);
    free(resp);
}

static void draw(int sel) {
    consoleClear();
    printf("FesLoader - mundos de RPG Maker Fes\n");
    printf("Modo: %s   (Y: cambiar)\n", mode == 0 ? "BAJAR del catalogo" : "SUBIR mis mundos");
    printf("Arriba/Abajo  A: %s  X: recargar  START: salir\n\n", mode == 0 ? "instalar" : "subir");
    int n = mode == 0 ? nworlds : nlocals;
    if (n == 0) printf("(nada que mostrar)\n");
    int top = sel - VISIBLE / 2;
    if (top > n - VISIBLE) top = n - VISIBLE;
    if (top < 0) top = 0;
    for (int i = top; i < n && i < top + VISIBLE; i++) {
        if (mode == 0) printf("%s %s (%lu KB)\n", i == sel ? ">" : " ", worlds[i].name, worlds[i].size / 1024);
        else printf("%s game_%02d.dat (%lu KB)\n", i == sel ? ">" : " ", locals[i].slot, locals[i].size / 1024);
    }
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
    refresh_local();

    int sel = 0;
    draw(sel);
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        int n = mode == 0 ? nworlds : nlocals;
        if (k & KEY_START) break;
        if ((k & KEY_DOWN) && sel + 1 < n) { sel++; draw(sel); }
        if ((k & KEY_UP) && sel > 0) { sel--; draw(sel); }
        if (k & KEY_Y) { mode = !mode; sel = 0; msg[0] = 0; draw(sel); }
        if (k & KEY_X) {
            msg[0] = 0; consoleClear(); printf("Recargando...\n");
            fetch_list(); refresh_local();
            n = mode == 0 ? nworlds : nlocals;
            if (sel >= n) sel = 0;
            draw(sel);
        }
        if ((k & KEY_A) && n > 0) {
            consoleClear(); printf("Trabajando, espera...\n");
            if (mode == 0) install(&worlds[sel]); else upload(&locals[sel]);
            refresh_local();
            draw(sel);
        }
        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    fsExit(); httpcExit(); acExit(); gfxExit();
    return 0;
}
