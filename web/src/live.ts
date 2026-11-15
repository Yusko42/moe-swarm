import { postChat } from './api/chat';
import { busyStates, type ViewModel } from './ui/states';

/** Минимальная проверка транспорта; не реализация полного жизненного цикла ФТ-7. */
export function connectChat(
  read: () => ViewModel,
  update: (next: ViewModel) => void,
) {
  let controller: AbortController | undefined;
  let started = false;
  const number = (value: number) =>
    value.toLocaleString('ru-RU', {
      minimumFractionDigits: 1,
      maximumFractionDigits: 1,
    });
  return {
    submit() {
      const view = read();
      if (controller || !view.draft.trim()) return;
      const prompt = view.draft.trim();
      const current = new AbortController();
      controller = current;
      started = false;
      view.messages.push({ role: 'user', text: prompt });
      view.draft = '';
      view.state = 'queued';
      view.notice = 'Ожидание подтверждения запроса';
      view.tone = 'info';
      update(view);
      void postChat({ prompt }, current.signal, (event) => {
        if (controller !== current || current.signal.aborted) return;
        switch (event.type) {
          case 'accepted':
            view.notice = `Вы в очереди: ${event.position}-й. Причина ожидания: ${event.reason}.`;
            break;
          case 'started':
            started = true;
            view.state = 'streaming';
            view.notice = '';
            view.messages.push({
              role: 'assistant',
              text: '',
              streaming: true,
            });
            break;
          case 'token': {
            const message = view.messages.at(-1);
            if (!started || message?.role !== 'assistant')
              throw new Error('И-2: token до started');
            message.text += event.text;
            break;
          }
          case 'done': {
            const message = view.messages.at(-1);
            if (!started || message?.role !== 'assistant')
              throw new Error('И-2: done до started');
            view.state = 'done';
            view.usage = event.context_percent;
            message.streaming = false;
            message.footer = `${number(event.tokens_per_second)} т/с, прогноз ${number(event.predicted_tokens_per_second)} т/с`;
            break;
          }
        }
        update(view);
      })
        .catch((error: unknown) => {
          if (current.signal.aborted || controller !== current) return;
          view.state = started ? 'disconnected' : 'error';
          view.notice =
            error instanceof Error ? error.message : 'Связь с роем потеряна';
          view.tone = 'error';
          if (!started) {
            view.messages.pop();
            view.draft = prompt;
          } else {
            const message = view.messages.at(-1)!;
            message.streaming = false;
            message.footer = 'Ответ не завершён';
          }
          update(view);
        })
        .finally(() => {
          if (controller === current) controller = undefined;
        });
    },
    stop() {
      const view = read();
      if (!controller || !busyStates.includes(view.state)) return;
      controller.abort();
      controller = undefined;
      view.state = 'stopped';
      view.notice = started ? '' : 'Запрос отменён';
      const message = view.messages.at(-1);
      if (message?.role === 'assistant') {
        message.streaming = false;
        message.footer = 'Ответ остановлен';
      }
      update(view);
    },
  };
}
