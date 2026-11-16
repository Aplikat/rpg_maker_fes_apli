// FesLoader v3 - catalogo de mundos de RPG Maker Fes con interfaz tactil.
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define BASE_URL "http://fes-test.muysimple001.workers.dev"
#define DEFAULT_EXTDATA_ID 0x1BD5u
#define MAX_WORLDS 300
#define NSLOTS 100
#define ROWS 5
#define ROW_Y0 38
#define ROW_H 34

typedef struct { char id[40]; char name[48]; char desc[160]; u32 size; } World;

static World worlds[MAX_WORLDS];
static int nworlds = 0;
static bool occ[NSLOTS];          // ranura ocupada?
static char slotname[NSLOTS][48]; // nombre del mundo en esa ranura
static u32 slotsize[NSLOTS];
static int noccupied = 0;
static u32 extdata_id = DEFAULT_EXTDATA_ID;
static char msg[640] = "";

enum { ST_LIST, ST_SLOTS, ST_CONFIRM };
static int state = ST_LIST, tab = 0, sel = 0, scrolltop = 0;
static int pending_world = -1, target_slot = -1;

// ---------- colores ----------
#define COL(r, g, b) C2D_Color32(r, g, b, 255)
static u32 cBg, cBg2, cPanel, cRow, cRowSel, cText, cDim, cBlue, cGreen, cRed, cTabOff, cWhite;

// ---------- graficos ----------
static C3D_RenderTarget *topT, *botT;
static C2D_TextBuf tbuf;

static void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.0f, w, h, c); }

static void txt(float x, float y, float sc, u32 c, const char *s) {
    C2D_Text t;
    C2D_TextParse(&t, tbuf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, sc, sc, c);
}
static void txtw(float x, float y, float sc, u32 c, const char *s, float wrap) {
    C2D_Text t;
    C2D_TextParse(&t, tbuf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x, y, 0.5f, sc, sc, c, wrap);
}
static void txtc(float cx, float y, float sc, u32 c, const char *s) {
    C2D_Text t;
    float w, h;
    C2D_TextParse(&t, tbuf, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, sc, sc, &w, &h);
    C2D_DrawText(&t, C2D_WithColor, cx - w / 2, y, 0.5f, sc, sc, c);
}
static void button(float x, float y, float w, float h, u32 col, const char *label) {
    rect(x, y, w, h, col);
    txtc(x + w / 2, y + h / 2 - 8, 0.5f, cWhite, label);
}
static void trunc_to(const char *src, char *dst, size_t max) {  // recorta sin partir UTF-8
    size_t n = strlen(src);
    if (n < max) { strcpy(dst, src); return; }
    size_t k = max - 3;
    while (k > 0 && ((unsigned char)src[k] & 0xC0) == 0x80) k--;
    memcpy(dst, src, k);
    strcpy(dst + k, "...");
}

static void say(const char *fmt, ...) {
    char line[200];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    strncat(msg, line, sizeof(msg) - strlen(msg) - 1);
    strncat(msg, "\n", sizeof(msg) - strlen(msg) - 1);
}

// ---------- red ----------
static Result http_request(HTTPC_RequestMethod method, const char *url, const u8 *body, u32 bodysize,
                           u32 *status, u8 **out, u32 *outsize) {
    httpcContext ctx;
    Result ret = httpcOpenContext(&ctx, method, url, 1);
    if (R_FAILED(ret)) { say("OpenContext: %08lX", (unsigned long)ret); return ret; }
    httpcAddRequestHeaderField(&ctx, "User-Agent", "Mozilla/5.0 FesLoader/3.0");
    httpcAddRequestHeaderField(&ctx, "Accept", "*/*");
    if (body) {
        httpcAddRequestHeaderField(&ctx, "Content-Type", "application/octet-stream");
        ret = httpcAddPostDataRaw(&ctx, (const u32 *)body, bodysize);
        if (R_FAILED(ret)) { say("PostData: %08lX", (unsigned long)ret); httpcCloseContext(&ctx); return ret; }
    }
    ret = httpcBeginRequest(&ctx);
    if (R_FAILED(ret)) { say("BeginRequest: %08lX", (unsigned long)ret); httpcCloseContext(&ctx); return ret; }
    *status = 0;
    ret = httpcGetResponseStatusCodeTimeout(&ctx, status, 20000000000ULL);
    if (R_FAILED(ret)) { say("Estado: %08lX", (unsigned long)ret); httpcCloseContext(&ctx); return ret; }

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
    if (R_FAILED(ret)) { say("Descarga: %08lX", (unsigned long)ret); free(buf); return ret; }
    u8 *nb = realloc(buf, size + 1);
    if (!nb) { free(buf); return -1; }
    nb[size] = 0;
    *out = nb; *outsize = size;
    return 0;
}

static void urlenc(const char *s, char *out, size_t outsz) {
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (; *s && o + 4 < outsz; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = c;
        else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
    }
    out[o] = 0;
}

// ---------- extdata ----------
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

// El nombre del mundo esta en UTF-16 unos 0xF8 bytes despues de la firma.
static void read_world_name(FS_Archive arc, int slot, char *out, size_t outsz) {
    out[0] = 0;
    char path[32];
    snprintf(path, sizeof(path), "/game_%02d.dat", slot);
    Handle fh;
    if (R_FAILED(FSUSER_OpenFile(&fh, arc, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0))) return;
    u8 buf[0x160];
    u32 rd = 0;
    FSFILE_Read(fh, &rd, 0, buf, sizeof(buf));
    FSFILE_Close(fh);
    int m = -1;
    for (int i = 0; i < 4 && i + 8 <= (int)rd; i++) if (memcmp(buf + i, "rtk0805a", 8) == 0) { m = i; break; }
    if (m < 0) return;
    size_t o = 0;
    for (int p = m + 0xF8; p + 1 < (int)rd && o + 4 < outsz; p += 2) {
        u32 c = buf[p] | (buf[p + 1] << 8);
        if (c == 0) break;
        if (c < 0x20) c = ' ';
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = 0xC0 | (c >> 6); out[o++] = 0x80 | (c & 0x3F); }
        else if (c >= 0xD800 && c < 0xE000) { /* ignorar sustitutos */ }
        else { out[o++] = 0xE0 | (c >> 12); out[o++] = 0x80 | ((c >> 6) & 0x3F); out[o++] = 0x80 | (c & 0x3F); }
        if (o > 40) break;
    }
    out[o] = 0;
}

static void scan_local(FS_Archive arc) {
    memset(occ, 0, sizeof(occ));
    noccupied = 0;
    Handle dir;
    if (R_FAILED(FSUSER_OpenDirectory(&dir, arc, fsMakePath(PATH_ASCII, "/")))) return;
    FS_DirectoryEntry ent[16];
    u32 n = 0;
    while (R_SUCCEEDED(FSDIR_Read(dir, &n, 16, ent)) && n > 0) {
        for (u32 i = 0; i < n; i++) {
            char name[32] = { 0 };
            for (int k = 0; k < 31 && ent[i].name[k]; k++) name[k] = (char)ent[i].name[k];
            int num;
            if (sscanf(name, "game_%d.dat", &num) == 1 && num >= 0 && num < NSLOTS && !occ[num]) {
                occ[num] = true;
                slotsize[num] = (u32)ent[i].fileSize;
                noccupied++;
            }
        }
    }
    FSDIR_Close(dir);
    for (int s = 0; s < NSLOTS; s++) {
        slotname[s][0] = 0;
        if (occ[s]) read_world_name(arc, s, slotname[s], sizeof(slotname[s]));
        if (occ[s] && !slotname[s][0]) strcpy(slotname[s], "(sin nombre)");
    }
}

static void refresh_local(void) {
    FS_Archive arc;
    Result r = open_extdata(&arc);
    if (R_FAILED(r)) {
        say("No pude abrir el extdata (ID %lX): %08lX", (unsigned long)extdata_id, (unsigned long)r);
        memset(occ, 0, sizeof(occ)); noccupied = 0;
        return;
    }
    scan_local(arc);
    FSUSER_CloseArchive(arc);
}

// n-esimo mundo local ocupado -> numero de ranura (o -1)
static int nth_occupied(int n) {
    for (int s = 0; s < NSLOTS; s++) if (occ[s] && n-- == 0) return s;
    return -1;
}
static int first_free(void) {
    for (int s = 0; s < NSLOTS; s++) if (!occ[s]) return s;
    return -1;
}

// ---------- catalogo ----------
static void fetch_list(void) {
    nworlds = 0;
    u32 wifi = 0;
    if (R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi == 0) { say("Sin Wi-Fi. Conectate y pulsa Recargar."); return; }
    char url[200];
    snprintf(url, sizeof(url), "%s/api/list", BASE_URL);
    u8 *data; u32 size, status;
    Result r = http_request(HTTPC_METHOD_GET, url, NULL, 0, &status, &data, &size);
    if (R_FAILED(r)) { say("Error al bajar la lista: %08lX", (unsigned long)r); return; }
    if (status != 200) { say("La lista respondio HTTP %lu", (unsigned long)status); free(data); return; }
    for (char *line = strtok((char *)data, "\n"); line && nworlds < MAX_WORLDS; line = strtok(NULL, "\n")) {
        World *w = &worlds[nworlds];
        char *t1 = strchr(line, '\t'); if (!t1) continue; *t1++ = 0;
        char *t2 = strchr(t1, '\t');   if (!t2) continue; *t2++ = 0;
        char *t3 = strchr(t2, '\t');
        if (t3) *t3++ = 0;
        snprintf(w->id, sizeof(w->id), "%s", line);
        snprintf(w->name, sizeof(w->name), "%s", t1);
        snprintf(w->desc, sizeof(w->desc), "%s", t3 ? t3 : "");
        w->size = (u32)atoi(t2);
        nworlds++;
    }
    free(data);
    if (nworlds == 0) say("El catalogo esta vacio.");
}

// slot < 0 = primera ranura libre. Si la ranura esta ocupada, la sobrescribe.
static void do_install(const World *w, int slot) {
    msg[0] = 0;
    char url[200];
    snprintf(url, sizeof(url), "%s/api/world/%s", BASE_URL, w->id);
    u8 *data; u32 size, status;
    Result r = http_request(HTTPC_METHOD_GET, url, NULL, 0, &status, &data, &size);
    if (R_FAILED(r)) { say("Error de descarga: %08lX", (unsigned long)r); return; }
    if (status != 200) { say("El servidor respondio HTTP %lu", (unsigned long)status); free(data); return; }

    FS_Archive arc;
    r = open_extdata(&arc);
    if (R_FAILED(r)) { say("No pude abrir el extdata: %08lX", (unsigned long)r); free(data); return; }
    scan_local(arc);
    if (slot < 0) slot = first_free();
    if (slot < 0) { say("No hay ranura libre."); FSUSER_CloseArchive(arc); free(data); return; }

    char path[32];
    snprintf(path, sizeof(path), "/game_%02d.dat", slot);
    if (occ[slot]) FSUSER_DeleteFile(arc, fsMakePath(PATH_ASCII, path));
    r = FSUSER_CreateFile(arc, fsMakePath(PATH_ASCII, path), 0, size);
    if (R_FAILED(r)) { say("No pude crear %s: %08lX", path, (unsigned long)r); FSUSER_CloseArchive(arc); free(data); return; }
    Handle fh;
    r = FSUSER_OpenFile(&fh, arc, fsMakePath(PATH_ASCII, path), FS_OPEN_WRITE, 0);
    if (R_SUCCEEDED(r)) {
        u32 written = 0;
        r = FSFILE_Write(fh, &written, 0, data, size, FS_WRITE_FLUSH);
        FSFILE_Close(fh);
        if (R_SUCCEEDED(r) && written == size) say("Listo: instalado en la ranura %02d", slot);
        else say("Error al escribir: %08lX", (unsigned long)r);
    } else {
        say("No pude abrir %s: %08lX", path, (unsigned long)r);
    }
    FSUSER_CloseArchive(arc);
    free(data);
}

static bool ask_text(const char *hint, const char *init, char *out, size_t outsz, int maxchars, bool allow_empty) {
    SwkbdState s;
    swkbdInit(&s, SWKBD_TYPE_NORMAL, 2, maxchars);
    swkbdSetHintText(&s, hint);
    if (init && init[0]) swkbdSetInitialText(&s, init);
    swkbdSetButton(&s, SWKBD_BUTTON_LEFT, "Cancelar", false);
    swkbdSetButton(&s, SWKBD_BUTTON_RIGHT, "Aceptar", true);
    swkbdSetValidation(&s, allow_empty ? SWKBD_ANYTHING : SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    return swkbdInputText(&s, out, outsz) == SWKBD_BUTTON_RIGHT;
}

static void do_upload(int slot, const char *name, const char *desc) {
    msg[0] = 0;
    FS_Archive arc;
    Result r = open_extdata(&arc);
    if (R_FAILED(r)) { say("No pude abrir el extdata: %08lX", (unsigned long)r); return; }
    char path[32];
    snprintf(path, sizeof(path), "/game_%02d.dat", slot);
    Handle fh;
    r = FSUSER_OpenFile(&fh, arc, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
    if (R_FAILED(r)) { say("No pude abrir %s: %08lX", path, (unsigned long)r); FSUSER_CloseArchive(arc); return; }
    u64 fsz = 0;
    FSFILE_GetSize(fh, &fsz);
    u8 *buf = malloc((size_t)fsz);
    u32 rd = 0;
    if (buf) r = FSFILE_Read(fh, &rd, 0, buf, (u32)fsz);
    FSFILE_Close(fh);
    FSUSER_CloseArchive(arc);
    if (!buf || R_FAILED(r) || rd != (u32)fsz) { say("No pude leer %s", path); free(buf); return; }

    char en[200], ed[500], url[900];
    urlenc(name, en, sizeof(en));
    urlenc(desc, ed, sizeof(ed));
    snprintf(url, sizeof(url), "%s/api/upload?name=%s&desc=%s", BASE_URL, en, ed);
    u8 *resp; u32 rsize, status;
    r = http_request(HTTPC_METHOD_POST, url, buf, rd, &status, &resp, &rsize);
    free(buf);
    if (R_FAILED(r)) { say("Error al subir: %08lX", (unsigned long)r); return; }
    for (u32 i = 0; i < rsize; i++) if (resp[i] == '\n') resp[i] = ' ';
    say("Servidor (%lu): %s", (unsigned long)status, resp);
    free(resp);
}

// ---------- interfaz ----------
static int list_count(void) {
    if (state == ST_SLOTS) return NSLOTS + 1;  // fila 0 = primera libre
    return tab == 0 ? nworlds : noccupied;
}
static void clamp_scroll(void) {
    int n = list_count();
    if (sel >= n) sel = n - 1;
    if (sel < 0) sel = 0;
    if (sel < scrolltop) scrolltop = sel;
    if (sel >= scrolltop + ROWS) scrolltop = sel - ROWS + 1;
    if (scrolltop < 0) scrolltop = 0;
}

static void draw_top(void) {
    rect(0, 0, 400, 30, cBlue);
    txt(10, 5, 0.65f, cWhite, "FesLoader - Mundos de RPG Maker Fes");
    rect(8, 36, 384, 128, cPanel);

    char line[200];
    if (state == ST_LIST && tab == 0 && nworlds > 0 && sel < nworlds) {
        const World *w = &worlds[sel];
        txtw(16, 42, 0.65f, cText, w->name, 368);
        snprintf(line, sizeof(line), "%lu KB", (unsigned long)(w->size / 1024));
        txt(16, 66, 0.5f, cDim, line);
        txtw(16, 88, 0.5f, cText, w->desc[0] ? w->desc : "(sin descripcion)", 368);
    } else if (state == ST_LIST && tab == 1 && noccupied > 0) {
        int s = nth_occupied(sel);
        if (s >= 0) {
            txtw(16, 42, 0.65f, cText, slotname[s], 368);
            snprintf(line, sizeof(line), "Ranura %02d  -  %lu KB", s, (unsigned long)(slotsize[s] / 1024));
            txt(16, 66, 0.5f, cDim, line);
            txtw(16, 88, 0.5f, cText, "Pulsa Subir para compartirlo. Te pedire nombre y descripcion.", 368);
        }
    } else if ((state == ST_SLOTS || state == ST_CONFIRM) && pending_world >= 0) {
        txt(16, 42, 0.5f, cDim, "Vas a instalar:");
        txtw(16, 62, 0.65f, cText, worlds[pending_world].name, 368);
        txtw(16, 88, 0.5f, cText, state == ST_CONFIRM ? "Esa ranura ya tiene un mundo. Si sigues, se reemplaza y no se puede recuperar." : "Elige en que ranura del juego guardarlo en la pantalla de abajo.", 368);
    } else {
        txt(16, 42, 0.55f, cDim, tab == 0 ? "No hay mundos en el catalogo." : "No tienes mundos en el juego.");
    }

    rect(8, 170, 384, 64, cPanel);
    txt(16, 174, 0.45f, cDim, "Estado:");
    txtw(16, 188, 0.45f, cText, msg[0] ? msg : "Listo.", 368);
}

static void draw_bottom(void) {
    if (state == ST_LIST) {
        button(6, 4, 150, 28, tab == 0 ? cBlue : cTabOff, "BAJAR");
        button(164, 4, 150, 28, tab == 1 ? cBlue : cTabOff, "SUBIR");
    } else {
        rect(0, 0, 320, 34, cBlue);
        txt(10, 8, 0.55f, cWhite, state == ST_CONFIRM ? "Confirmar" : "Elige la ranura");
        button(236, 4, 78, 28, cRed, "Cancelar");
    }

    if (state == ST_CONFIRM) {
        char l1[120];
        snprintf(l1, sizeof(l1), "Ranura %02d: %s", target_slot, slotname[target_slot]);
        rect(6, 50, 308, 60, cPanel);
        txtw(14, 56, 0.55f, cText, l1, 292);
        txtw(14, 80, 0.45f, cDim, "Se borrara ese mundo y se pondra el nuevo.", 292);
        button(14, 130, 140, 40, cRed, "Si, sobrescribir");
        button(166, 130, 140, 40, cTabOff, "No");
        return;
    }

    int n = list_count();
    for (int v = 0; v < ROWS; v++) {
        int i = scrolltop + v;
        if (i >= n) break;
        float y = ROW_Y0 + v * ROW_H;
        rect(6, y, 308, ROW_H - 4, i == sel ? cRowSel : cRow);
        char a[64], b[64], t[64];
        const char *lab = "Bajar";
        u32 bc = cGreen;
        if (state == ST_LIST && tab == 0) {
            trunc_to(worlds[i].name, a, 26);
            snprintf(b, sizeof(b), "%lu KB", (unsigned long)(worlds[i].size / 1024));
        } else if (state == ST_LIST) {
            int s = nth_occupied(i);
            trunc_to(slotname[s], t, 22);
            snprintf(a, sizeof(a), "%02d: %s", s, t);
            snprintf(b, sizeof(b), "%lu KB", (unsigned long)(slotsize[s] / 1024));
            lab = "Subir";
        } else if (i == 0) {
            strcpy(a, "Primera ranura libre");
            strcpy(b, "automatico");
            lab = "Elegir";
        } else {
            int s = i - 1;
            if (occ[s]) {
                trunc_to(slotname[s], t, 20);
                snprintf(a, sizeof(a), "Ranura %02d: %s", s, t);
                snprintf(b, sizeof(b), "ocupada - %lu KB", (unsigned long)(slotsize[s] / 1024));
                lab = "Reemplazar"; bc = cRed;
            } else {
                snprintf(a, sizeof(a), "Ranura %02d", s);
                strcpy(b, "libre");
                lab = "Elegir";
            }
        }
        txt(12, y + 2, 0.5f, cText, a);
        txt(12, y + 16, 0.42f, cDim, b);
        button(232, y + 3, 76, 24, bc, lab);
    }
    if (n == 0) txtc(160, 110, 0.5f, cDim, tab == 0 ? "(catalogo vacio)" : "(no hay mundos)");

    button(6, 212, 50, 24, cTabOff, "^");
    button(62, 212, 50, 24, cTabOff, "v");
    if (state == ST_LIST) button(118, 212, 196, 24, cTabOff, "Recargar");
}

static void render(void) {
    C2D_TextBufClear(tbuf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(topT, cBg);
    C2D_SceneBegin(topT);
    draw_top();
    C2D_TargetClear(botT, cBg2);
    C2D_SceneBegin(botT);
    draw_bottom();
    C3D_FrameEnd(0);
}

static void busy(const char *text) {
    msg[0] = 0;
    say("%s", text);
    render();
}

static bool inside(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }

static void reload_all(void) {
    busy("Recargando...");
    msg[0] = 0;
    fetch_list();
    refresh_local();
    clamp_scroll();
}

static void activate(int i) {  // el boton de la fila i
    if (state == ST_LIST && tab == 0) {
        if (i < 0 || i >= nworlds) return;
        pending_world = i; state = ST_SLOTS; sel = 0; scrolltop = 0; msg[0] = 0;
    } else if (state == ST_LIST) {
        int s = nth_occupied(i);
        if (s < 0) return;
        char name[200] = "", desc[500] = "";
        if (!ask_text("Nombre del mundo", slotname[s], name, sizeof(name), 40, false)) return;
        if (!ask_text("Descripcion (opcional)", "", desc, sizeof(desc), 120, true)) return;
        busy("Subiendo...");
        do_upload(s, name, desc);
    } else if (state == ST_SLOTS) {
        if (i == 0) { busy("Descargando..."); do_install(&worlds[pending_world], -1); refresh_local(); state = ST_LIST; sel = pending_world; clamp_scroll(); }
        else if (occ[i - 1]) { target_slot = i - 1; state = ST_CONFIRM; }
        else { busy("Descargando..."); do_install(&worlds[pending_world], i - 1); refresh_local(); state = ST_LIST; sel = pending_world; clamp_scroll(); }
    }
}

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    cfguInit();
    acInit();
    httpcInit(0x100000);
    fsInit();
    topT = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    botT = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    tbuf = C2D_TextBufNew(4096);
    cBg = COL(20, 24, 42); cBg2 = COL(28, 32, 54); cPanel = COL(38, 44, 72);
    cRow = COL(46, 52, 84); cRowSel = COL(66, 78, 128); cText = COL(240, 240, 250); cDim = COL(160, 168, 200);
    cBlue = COL(52, 96, 200); cGreen = COL(40, 150, 90); cRed = COL(190, 60, 60);
    cTabOff = COL(70, 76, 104); cWhite = COL(255, 255, 255);

    load_config();
    busy("Conectando...");
    msg[0] = 0;
    fetch_list();
    refresh_local();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();
        touchPosition tp;
        hidTouchRead(&tp);
        int n = list_count();

        if (kd & KEY_START) break;
        if (kd & KEY_DOWN) { sel++; clamp_scroll(); }
        if (kd & KEY_UP) { sel--; clamp_scroll(); }
        if (state == ST_LIST && (kd & (KEY_L | KEY_R))) { tab = !tab; sel = 0; scrolltop = 0; msg[0] = 0; }
        if ((kd & KEY_B) && state != ST_LIST) { state = ST_LIST; clamp_scroll(); }
        if ((kd & KEY_A) && state != ST_CONFIRM && n > 0) activate(sel);

        if (kd & KEY_TOUCH) {
            int x = tp.px, y = tp.py;
            if (state == ST_CONFIRM) {
                if (inside(x, y, 14, 130, 140, 40)) {
                    busy("Descargando...");
                    do_install(&worlds[pending_world], target_slot);
                    refresh_local(); state = ST_LIST; sel = pending_world; clamp_scroll();
                } else if (inside(x, y, 166, 130, 140, 40) || inside(x, y, 236, 4, 78, 28)) {
                    state = ST_SLOTS;
                }
            } else {
                if (state == ST_LIST && inside(x, y, 6, 4, 150, 28)) { tab = 0; sel = 0; scrolltop = 0; msg[0] = 0; }
                else if (state == ST_LIST && inside(x, y, 164, 4, 150, 28)) { tab = 1; sel = 0; scrolltop = 0; msg[0] = 0; }
                else if (state != ST_LIST && inside(x, y, 236, 4, 78, 28)) { state = ST_LIST; clamp_scroll(); }
                else if (inside(x, y, 6, 212, 50, 24)) { sel -= ROWS; clamp_scroll(); }
                else if (inside(x, y, 62, 212, 50, 24)) { sel += ROWS; clamp_scroll(); }
                else if (state == ST_LIST && inside(x, y, 118, 212, 196, 24)) reload_all();
                else if (y >= ROW_Y0 && y < ROW_Y0 + ROWS * ROW_H) {
                    int i = scrolltop + (y - ROW_Y0) / ROW_H;
                    if (i < n) {
                        if (x >= 232) activate(i);
                        else sel = i;
                    }
                }
            }
        }
        render();
    }

    C2D_TextBufDelete(tbuf);
    C2D_Fini(); C3D_Fini();
    fsExit(); httpcExit(); acExit(); cfguExit(); gfxExit();
    return 0;
}
