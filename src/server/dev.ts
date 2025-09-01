import { Hono } from 'hono';
import { serve } from 'bun';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import config from '../../cash.config.ts';
import { renderCash } from '../compiler/index';

export type RunningServer = { port: number; stop: () => void };

export function createApp() {
	const app = new Hono();

	app.get('/public/*', async (c) => {
		const p = c.req.path.replace('/public', '');
		const filePath = path.join(config.publicDir, p);
		try {
			const data = await readFile(filePath);
			return new Response(data);
		} catch {
			return c.notFound();
		}
	});

	app.post('/__action/:name', async (c) => {
		const { name } = c.req.param();
		const form = await c.req.formData();
		// Echo action for MVP
		return c.html(`<!doctype html><html><body><p>Action: ${name}</p><pre>${JSON.stringify(Object.fromEntries(form as any), null, 2)}</pre><a href="/">Back</a></body></html>`);
	});

	app.get('/*', async (c) => {
		const url = new URL(c.req.url);
		// Resolve by $route markers across pages
		let filePath = path.join(config.pagesDir, url.pathname === '/' ? 'index.cash' : `${url.pathname}.cash`);
		if (!(await fileExists(filePath))) {
			// naive scan for $route "path" in pages
			const candidates = await scanRoutes(config.pagesDir);
			const match = candidates.find(c => c.route === url.pathname);
			if (match) filePath = match.file;
		}
		try {
			const src = await readFile(filePath, 'utf8');
			const params = Object.fromEntries(url.searchParams.entries());
			const items = typeof params.items === 'string' && params.items.length
				? params.items.split(',')
				: ['alpha', 'beta', 'gamma'];
			const name = params.name || 'Cash';
			const ctx = { name, items };
			const { html, head } = renderCash(src, ctx);
			const headHtml = [
				head.title ? `<title>${head.title}</title>` : '',
				...(head.meta?.map((m) => `<meta ${Object.entries(m).map(([k,v])=>`${k}="${String(v)}"`).join(' ')}>` ) || [])
			].join('\n');
			const full = `<!doctype html><html><head>${headHtml}</head><body>${html}</body></html>`;
			return c.html(full);
		} catch (e) {
			return c.text('Not found', 404);
		}
	});

	return app;
}

export function startDevServer(port: number = (config as any).port || 3000): RunningServer {
	const app = createApp();
	const server = serve({
		port,
		fetch: app.fetch,
	});
	console.log(`Cash dev server running on http://localhost:${server.port}`);
	return {
		port: server.port,
		stop: () => server.stop && server.stop(),
	};
}

async function fileExists(p: string): Promise<boolean> {
	try {
		await readFile(p);
		return true;
	} catch {
		return false;
	}
}

async function scanRoutes(dir: string): Promise<Array<{ route: string; file: string }>> {
	const out: Array<{ route: string; file: string }> = [];
	// Very small scan: only look at index.cash and top-level .cash files
	const files = ['index.cash'];
	for (const f of files) {
		const full = path.join(dir, f);
		try {
			const src = await readFile(full, 'utf8');
			const m = src.match(/\$route\s+["']([^"]+)["']/);
			if (m) out.push({ route: m[1], file: full });
		} catch {}
	}
	return out;
}



