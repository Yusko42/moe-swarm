export interface SseFrame {
  event: string;
  data: string;
}

/** Разбирает SSE независимо от границ UTF-8/HTTP-чанков, с LF, CRLF и CR. */
export class SseDecoder {
  private line = '';
  private event = '';
  private data: string[] = [];
  private skipLf = false;
  private size = 0;

  push(chunk: string): SseFrame[] {
    const frames: SseFrame[] = [];
    for (const char of chunk) {
      if (this.skipLf && char === '\n') {
        this.skipLf = false;
        continue;
      }
      this.skipLf = false;
      if (char === '\r' || char === '\n') {
        const frame = this.finishLine();
        if (frame) frames.push(frame);
        this.skipLf = char === '\r';
      } else {
        this.line += char;
        this.size += 1;
        if (this.size > 1_048_576)
          throw new Error('Слишком большое событие SSE');
      }
    }
    return frames;
  }

  private finishLine(): SseFrame | undefined {
    const line = this.line;
    this.line = '';
    if (!line) {
      const frame = this.data.length
        ? { event: this.event || 'message', data: this.data.join('\n') }
        : undefined;
      this.event = '';
      this.data = [];
      this.size = 0;
      return frame;
    }
    if (line.startsWith(':')) return;
    const colon = line.indexOf(':');
    const key = colon < 0 ? line : line.slice(0, colon);
    const raw = colon < 0 ? '' : line.slice(colon + 1);
    const value = raw.startsWith(' ') ? raw.slice(1) : raw;
    if (key === 'event') this.event = value;
    if (key === 'data') this.data.push(value);
  }
}

export async function* readSse(
  body: ReadableStream<Uint8Array>,
): AsyncGenerator<SseFrame> {
  const reader = body.getReader();
  const utf8 = new TextDecoder('utf-8', { fatal: true });
  const parser = new SseDecoder();
  try {
    while (true) {
      const { done, value } = await reader.read();
      const text = done ? utf8.decode() : utf8.decode(value, { stream: true });
      for (const frame of parser.push(text)) yield frame;
      // Незавершённое событие при EOF не доставляется согласно формату SSE.
      if (done) return;
    }
  } finally {
    await reader.cancel().catch(() => undefined);
    reader.releaseLock();
  }
}
