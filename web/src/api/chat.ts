import { decodeEvent, type ChatEvent, type ChatRequest } from './events';
import { readSse } from './sse';

export async function postChat(
  request: ChatRequest,
  signal: AbortSignal,
  receive: (event: ChatEvent) => void,
): Promise<void> {
  const response = await fetch('/api/v1/chat', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Accept: 'text/event-stream',
    },
    body: JSON.stringify(request),
    signal,
    credentials: 'same-origin',
  });
  if (!response.ok) throw new Error(`Ошибка И-2: HTTP ${response.status}`);
  if (
    response.headers.get('content-type')?.split(';')[0].trim().toLowerCase() !==
    'text/event-stream'
  ) {
    await response.body?.cancel();
    throw new Error('И-2: ожидался поток SSE');
  }
  if (!response.body) throw new Error('И-2: пустой поток ответа');
  let phase: 'new' | 'accepted' | 'started' = 'new';
  for await (const frame of readSse(response.body)) {
    const event = decodeEvent(frame.event, frame.data);
    if (event.type === 'accepted' && phase !== 'started') phase = 'accepted';
    else if (event.type === 'started' && phase === 'accepted')
      phase = 'started';
    else if (
      (event.type !== 'token' && event.type !== 'done') ||
      phase !== 'started'
    )
      throw new Error('И-2: нарушен порядок событий');
    receive(event);
    if (event.type === 'done') return;
  }
  throw new Error('Связь с роем потеряна');
}
