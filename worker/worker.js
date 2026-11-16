// worker.js - Catálogo de mundos de RPG Maker Fes (Cloudflare Workers + KV).
//
// Necesita: un KV namespace enlazado como WORLDS y un secreto ADMIN_TOKEN.
//
// Público (lo usa la app de la 3DS):
//   GET /api/list         -> líneas "id<TAB>nombre<TAB>tamaño"
//   GET /api/world/<id>   -> el archivo game_NN.dat tal cual
// Admin (subir mundos; cabecera "Authorization: Bearer <ADMIN_TOKEN>"):
//   PUT    /admin/world/<id>?name=Mi%20mundo   (cuerpo = el .dat)
//   DELETE /admin/world/<id>
// <id> solo letras, números, guion y guion bajo (máx. 32).

const ID_RE = /^[A-Za-z0-9_-]{1,32}$/;

function text(body, status = 200) {
  return new Response(body, { status, headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
}

async function readIndex(env) {
  return (await env.WORLDS.get('index', 'json')) || {};
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const parts = url.pathname.replace(/\/+$/, '').split('/').filter(Boolean);

    if (parts[0] === 'api' && parts[1] === 'list' && request.method === 'GET') {
      const index = await readIndex(env);
      const lines = Object.entries(index).map(([id, w]) => `${id}\t${w.name.replace(/[\t\r\n]/g, ' ')}\t${w.size}`);
      return text(lines.join('\n') + '\n');
    }

    if (parts[0] === 'api' && parts[1] === 'world' && request.method === 'GET') {
      const id = parts[2] || '';
      if (!ID_RE.test(id)) return text('id invalido', 400);
      const data = await env.WORLDS.get('w:' + id, 'arrayBuffer');
      if (!data) return text('no existe', 404);
      return new Response(data, { headers: { 'Content-Type': 'application/octet-stream' } });
    }

    if (parts[0] === 'admin' && parts[1] === 'world') {
      if (request.headers.get('Authorization') !== 'Bearer ' + env.ADMIN_TOKEN) return text('no autorizado', 401);
      const id = parts[2] || '';
      if (!ID_RE.test(id)) return text('id invalido', 400);
      const index = await readIndex(env);

      if (request.method === 'PUT') {
        const data = new Uint8Array(await request.arrayBuffer());
        // Un mundo válido empieza con "rtk0805a" (con o sin 2 bytes antes).
        const head = new TextDecoder('latin1').decode(data.slice(0, 12));
        if (!head.includes('rtk0805a')) return text('no parece un mundo de RPG Maker Fes', 400);
        await env.WORLDS.put('w:' + id, data);
        index[id] = { name: url.searchParams.get('name') || id, size: data.length };
        await env.WORLDS.put('index', JSON.stringify(index));
        return text('ok\n');
      }
      if (request.method === 'DELETE') {
        await env.WORLDS.delete('w:' + id);
        delete index[id];
        await env.WORLDS.put('index', JSON.stringify(index));
        return text('ok\n');
      }
    }

    return text('FesLoader catalogo funcionando\n');
  },
};
