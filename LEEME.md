# FesLoader

## 1. Worker (Cloudflare)
1. Crea un KV namespace (Workers & Pages > KV) y enlázalo al Worker con el nombre `WORLDS`.
2. Agrega el secreto `ADMIN_TOKEN` (una clave larga que inventes).
3. Pega `worker/worker.js`. Abre la URL: debe decir "FesLoader catalogo funcionando".
4. Sube un mundo:
   curl -X PUT -H "Authorization: Bearer TU_TOKEN" --data-binary @game_00.dat "https://TU.workers.dev/admin/world/prueba?name=Prueba"
   Prueba: https://TU.workers.dev/api/list

## 2. App 3DS
- Cambia `BASE_URL` en `source/main.c` si tu Worker tiene otra dirección.
- Sube esta carpeta a un repositorio de GitHub; Actions compila y deja `FesLoader.3dsx` en Artifacts.
- Copia el `.3dsx` a `sdmc:/3ds/` y ábrelo desde Homebrew Launcher.
- Si no abre el extdata, crea `sdmc:/3ds/FesLoader/extdata_id.txt` con el ID real en hexadecimal.
