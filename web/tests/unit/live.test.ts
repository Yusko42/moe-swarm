import { afterEach, describe, expect, it, vi } from 'vitest';
import { connectChat } from '../../src/live';
import { fixture, type ViewModel } from '../../src/ui/states';

afterEach(() => vi.unstubAllGlobals());

const accepted =
  'event: accepted\ndata: {"request_id":"r1","position":2,"reason":"Все исполнители заняты"}\n\n';
const started = 'event: started\ndata: {"request_id":"r1"}\n\n';
const token = 'event: token\ndata: {"text":"Ответ"}\n\n';
const done =
  'event: done\ndata: {"tokens_per_second":10,"predicted_tokens_per_second":12,"context_percent":5}\n\n';

function response(wire: string) {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue(
      new Response(wire, {
        headers: { 'Content-Type': 'text/event-stream' },
      }),
    ),
  );
}

function setup(onUpdate?: (view: ViewModel) => void) {
  let view = fixture('ready');
  const actions = connectChat(
    () => view,
    (next) => {
      view = next;
      onUpdate?.(view);
    },
  );
  return { actions, read: () => view };
}

describe('isolated live controller', () => {
  it('shows queued, streaming, done and final answer metrics', async () => {
    response(accepted + started + token + done);
    const states: string[] = [];
    const { actions, read } = setup((view) => states.push(view.state));
    actions.submit();
    await vi.waitFor(() => expect(read().state).toBe('done'));
    expect(states).toContain('queued');
    expect(states).toContain('streaming');
    expect(read().messages.at(-1)?.text).toBe('Ответ');
    expect(read().messages.at(-1)?.footer).toBe('10,0 т/с, прогноз 12,0 т/с');
    expect(read().usage).toBe(5);
  });

  it('restores draft on an error before started', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(new Response('busy', { status: 503 })),
    );
    const { actions, read } = setup();
    const draft = read().draft;
    actions.submit();
    await vi.waitFor(() => expect(read().state).toBe('error'));
    expect(read().draft).toBe(draft);
    expect(read().messages).toEqual([]);
  });

  it('retains partial answer on unexpected EOF', async () => {
    response(accepted + started + token);
    const { actions, read } = setup();
    actions.submit();
    await vi.waitFor(() => expect(read().state).toBe('disconnected'));
    expect(read().messages.at(-1)?.text).toBe('Ответ');
    expect(read().messages.at(-1)?.footer).toBe('Ответ не завершён');
    expect(read().notice).toBe('Связь с роем потеряна');
  });

  it('stops after first token and ignores later queued tokens', async () => {
    response(accepted + started + token + token + done);
    let stop = () => {};
    const { actions, read } = setup((view) => {
      if (view.state === 'streaming' && view.messages.at(-1)?.text) stop();
    });
    stop = actions.stop;
    actions.submit();
    await vi.waitFor(() => expect(read().state).toBe('stopped'));
    expect(read().messages.at(-1)?.text).toBe('Ответ');
    expect(read().messages.at(-1)?.footer).toBe('Ответ остановлен');
  });
});
