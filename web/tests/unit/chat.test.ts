import { afterEach, describe, expect, it, vi } from 'vitest';
import { postChat } from '../../src/api/chat';

afterEach(() => vi.unstubAllGlobals());

const accepted =
  'event: accepted\ndata: {"request_id":"r1","position":1,"reason":"busy"}\n\n';
const started = 'event: started\ndata: {"request_id":"r1"}\n\n';
const token = 'event: token\ndata: {"text":"Привет"}\n\n';
const done =
  'event: done\ndata: {"tokens_per_second":10,"predicted_tokens_per_second":12,"context_percent":5}\n\n';

describe('POST chat', () => {
  it('sends POST and consumes ordered SSE through done', async () => {
    const fetch = vi.fn().mockResolvedValue(
      new Response(accepted + started + token + done, {
        headers: { 'Content-Type': 'text/event-stream; charset=utf-8' },
      }),
    );
    vi.stubGlobal('fetch', fetch);
    const signal = new AbortController().signal;
    const receive = vi.fn();
    await postChat({ prompt: 'Вопрос' }, signal, receive);
    expect(fetch).toHaveBeenCalledWith(
      '/api/v1/chat',
      expect.objectContaining({
        method: 'POST',
        body: '{"prompt":"Вопрос"}',
        signal,
      }),
    );
    expect(receive.mock.calls.map(([event]) => event.type)).toEqual([
      'accepted',
      'started',
      'token',
      'done',
    ]);
  });

  it.each([
    accepted + started + token,
    accepted + started + 'event: token\ndata: {"text":"lost"}',
    '',
  ])('rejects EOF before done', async (wire) => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(
        new Response(wire, {
          headers: { 'Content-Type': 'text/event-stream' },
        }),
      ),
    );
    await expect(
      postChat({ prompt: 'x' }, new AbortController().signal, () => {}),
    ).rejects.toThrow('Связь с роем потеряна');
  });

  it('rejects a token before started', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(
        new Response(token, {
          headers: { 'Content-Type': 'text/event-stream' },
        }),
      ),
    );
    await expect(
      postChat({ prompt: 'x' }, new AbortController().signal, () => {}),
    ).rejects.toThrow('порядок событий');
  });

  it.each([
    new Response('bad', { status: 503 }),
    new Response('{}', { headers: { 'Content-Type': 'application/json' } }),
  ])('rejects unsuccessful or non-SSE responses', async (response) => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response));
    await expect(
      postChat({ prompt: 'x' }, new AbortController().signal, () => {}),
    ).rejects.toThrow();
  });

  it('cancels an open reader immediately after done', async () => {
    const cancel = vi.fn();
    const body = new ReadableStream({
      start(controller) {
        controller.enqueue(new TextEncoder().encode(accepted + started + done));
      },
      cancel,
    });
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(
        new Response(body, {
          headers: { 'Content-Type': 'text/event-stream' },
        }),
      ),
    );
    await postChat({ prompt: 'x' }, new AbortController().signal, () => {});
    expect(cancel).toHaveBeenCalledOnce();
  });
});
