import { describe, expect, it } from 'vitest';
import { SseDecoder, readSse } from '../../src/api/sse';
import { decodeEvent } from '../../src/api/events';

describe('SSE framing', () => {
  it.each(['\n', '\r\n', '\r'])(
    'supports line ending %j at arbitrary chunk boundaries',
    (eol) => {
      const decoder = new SseDecoder();
      const wire = [
        ': keepalive',
        'event: token',
        'data: first',
        'data: second',
        '',
        '',
      ].join(eol);
      const frames = [...wire].flatMap((char) => decoder.push(char));
      expect(frames).toEqual([{ event: 'token', data: 'first\nsecond' }]);
    },
  );

  it('ignores comments and fields, does not dispatch unfinished events', () => {
    const decoder = new SseDecoder();
    expect(
      decoder.push(': ping\n\nid: 1\nretry: 1000\nevent: token\ndata: x'),
    ).toEqual([]);
    expect(decoder.push('\n\n')).toEqual([{ event: 'token', data: 'x' }]);
  });

  it('retains empty data and defaults event name', () => {
    expect(new SseDecoder().push('data:\n\n')).toEqual([
      { event: 'message', data: '' },
    ]);
  });

  it('decodes Cyrillic characters split across UTF-8 bytes', async () => {
    const bytes = new TextEncoder().encode(
      'event: token\ndata: {"text":"Привет 👋"}\n\n',
    );
    const stream = new ReadableStream<Uint8Array>({
      start(controller) {
        for (const byte of bytes) controller.enqueue(Uint8Array.of(byte));
        controller.close();
      },
    });
    const events = [];
    for await (const frame of readSse(stream))
      events.push(decodeEvent(frame.event, frame.data));
    expect(events).toEqual([{ type: 'token', text: 'Привет 👋' }]);
  });

  it('rejects oversized events', () => {
    expect(() => new SseDecoder().push('x'.repeat(1_048_577))).toThrow(
      'Слишком большое',
    );
  });
});

describe('draft I-2 schema', () => {
  it.each([
    ['token', '{}'],
    ['done', '{"context_percent":101}'],
    ['unknown', '{}'],
    ['token', '[]'],
    ['token', 'null'],
    ['token', 'not-json'],
  ])('rejects invalid %s / %s', (type, data) =>
    expect(() => decodeEvent(type, data)).toThrow(),
  );
});
