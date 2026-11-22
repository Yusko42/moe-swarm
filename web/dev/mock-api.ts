import type { Plugin } from 'vite';
import type {
  ChatEvent,
  StateResponse,
  SwarmResponse,
} from '../src/api/events';

/** Только dev middleware. В dist не включается. */
export function mockApi(): Plugin {
  return {
    name: 'swarm-dev-mock',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use(async (request, response, next) => {
        const path = request.url?.split('?')[0];
        if (path === '/api/v1/state' || path === '/api/v1/swarm') {
          if (request.method !== 'GET') {
            response.writeHead(405).end();
            return;
          }
          const data: StateResponse | SwarmResponse = path.endsWith('/state')
            ? { status: 'ready' }
            : { status: 'active', nodes: 4 };
          response.writeHead(200, { 'Content-Type': 'application/json' });
          response.end(JSON.stringify(data));
          return;
        }
        if (path !== '/api/v1/chat') {
          next();
          return;
        }
        if (request.method !== 'POST') {
          response.writeHead(405).end();
          return;
        }
        let body = '';
        try {
          for await (const chunk of request) {
            body += String(chunk);
            if (Buffer.byteLength(body) > 65_536) {
              response.writeHead(413).end();
              return;
            }
          }
          const data: unknown = JSON.parse(body);
          if (
            !data ||
            typeof data !== 'object' ||
            !('prompt' in data) ||
            typeof data.prompt !== 'string' ||
            !data.prompt.trim()
          ) {
            response.writeHead(400).end();
            return;
          }
        } catch {
          response.writeHead(400).end();
          return;
        }

        response.writeHead(200, {
          'Content-Type': 'text/event-stream; charset=utf-8',
          'Cache-Control': 'no-cache',
          Connection: 'keep-alive',
          'X-Accel-Buffering': 'no',
        });
        response.flushHeaders();
        const emit = (event: ChatEvent) => {
          const { type, ...data } = event;
          response.write(`event: ${type}\ndata: ${JSON.stringify(data)}\n\n`);
        };
        emit({
          type: 'accepted',
          request_id: 'mock-1',
          position: 2,
          reason: 'Все исполнители заняты',
        });
        const tokens =
          'Рой — это несколько компьютеров, которые совместно выполняют одну задачу.\n\nЗапрос поступает в очередь. Затем свободные узлы начинают вычисления, и ответ появляется здесь постепенно, токен за токеном.\n\nЭта демонстрация работает локально и не отправляет ваши сообщения в интернет.'.match(
            /\S+\s*/g,
          ) ?? [];
        let timer: ReturnType<typeof setTimeout>;
        let index = 0;
        const tick = () => {
          if (response.destroyed) return;
          if (index < tokens.length) {
            emit({ type: 'token', text: tokens[index++] });
            timer = setTimeout(tick, 75);
          } else {
            emit({
              type: 'done',
              tokens_per_second: 13.3,
              predicted_tokens_per_second: 15.2,
              context_percent: 12,
            });
            response.end();
          }
        };
        timer = setTimeout(() => {
          if (response.destroyed) return;
          emit({ type: 'started', request_id: 'mock-1' });
          tick();
        }, 900);
        response.on('close', () => clearTimeout(timer));
      });
    },
  };
}
