import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createServer, type ViteDevServer } from 'vite';
import { mockApi } from '../../dev/mock-api';
import { readSse } from '../../src/api/sse';
import { decodeEvent } from '../../src/api/events';

describe('actual dev HTTP mock', () => {
  let server: ViteDevServer;
  let base: string;
  beforeAll(async () => {
    server = await createServer({
      configFile: false,
      plugins: [mockApi()],
      server: { host: '127.0.0.1', port: 0, watch: null, hmr: false },
    });
    await server.listen();
    const address = server.httpServer!.address();
    if (!address || typeof address === 'string')
      throw new Error('Missing mock port');
    base = `http://127.0.0.1:${address.port}`;
  });
  afterAll(async () => {
    await server?.close();
  });

  it('streams accepted, started, tokens, done over HTTP POST', async () => {
    const response = await fetch(`${base}/api/v1/chat`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ prompt: 'Как работает рой?' }),
    });
    expect(response.headers.get('content-type')).toContain('text/event-stream');
    const events = [];
    for await (const frame of readSse(response.body!))
      events.push(decodeEvent(frame.event, frame.data));
    expect(events[0].type).toBe('accepted');
    expect(events[1].type).toBe('started');
    expect(events.at(-1)?.type).toBe('done');
    expect(events.slice(2, -1).every((event) => event.type === 'token')).toBe(
      true,
    );
    expect(
      events
        .filter((event) => event.type === 'token')
        .map((event) => event.text)
        .join(''),
    ).toContain('несколько компьютеров');
  }, 15_000);

  it('serves local state and swarm fixtures', async () => {
    expect(await (await fetch(`${base}/api/v1/state`)).json()).toEqual({
      status: 'ready',
    });
    expect(await (await fetch(`${base}/api/v1/swarm`)).json()).toEqual({
      status: 'active',
      nodes: 4,
    });
  });

  it('rejects unsupported methods and invalid requests', async () => {
    expect((await fetch(`${base}/api/v1/chat`)).status).toBe(405);
    expect(
      (await fetch(`${base}/api/v1/state`, { method: 'POST' })).status,
    ).toBe(405);
    expect(
      (
        await fetch(`${base}/api/v1/chat`, {
          method: 'POST',
          body: '{"prompt":"  "}',
        })
      ).status,
    ).toBe(400);
    expect(
      (await fetch(`${base}/api/v1/chat`, { method: 'POST', body: 'bad-json' }))
        .status,
    ).toBe(400);
  });
});
