export const stateNames = {
  empty: 'Пустое поле',
  ready: 'Готов к отправке',
  queued: 'В очереди',
  streaming: 'Идёт ответ',
  done: 'Ответ завершён',
  stopped: 'Ответ остановлен',
  limit: 'Предел диалога',
  error: 'Ошибка до начала ответа',
  rebuilding: 'Перестройка во время ответа',
  retrying: 'Повтор после перестройки',
  'model-stopped': 'Модель остановлена',
  disconnected: 'Связь потеряна',
  disbanded: 'Рой распущен',
} as const;

export type StateId = keyof typeof stateNames;
export interface Message {
  role: 'user' | 'assistant';
  text: string;
  footer?: string;
  streaming?: boolean;
}
export interface ViewModel {
  state: StateId;
  draft: string;
  messages: Message[];
  notice: string;
  tone: 'info' | 'warning' | 'error';
  usage: number;
}
export const busyStates: StateId[] = [
  'queued',
  'streaming',
  'rebuilding',
  'retrying',
];
export const question =
  'Объясни простыми словами, как работает распределённый инференс.';
export const answer =
  'Представьте, что большую модель нужно запустить на нескольких компьютерах. Каждый из них берёт на себя часть вычислений — вместе они образуют рой.\n\nКак проходит запрос\n\n1. Вы отправляете вопрос. Он попадает в общую очередь.\n2. Свободные узлы получают задачу и совместно вычисляют ответ.\n3. Готовые фрагменты текста приходят в чат постепенно.\n\nГлавная идея: ресурсы нескольких устройств объединяются, чтобы работать с моделью, которая не помещается на одном из них.';
const partial =
  'Представьте, что большую модель нужно запустить на нескольких компьютерах. Каждый из них берёт на себя часть вычислений';

export function isState(value: string | null): value is StateId {
  return value !== null && Object.hasOwn(stateNames, value);
}

export function fixture(state: StateId, errorCode = '5.2'): ViewModel {
  const view: ViewModel = {
    state,
    draft: '',
    messages: [],
    notice: '',
    tone: 'info',
    usage: 0,
  };
  if (state === 'empty') return view;
  if (state === 'ready') {
    view.draft = question;
    return view;
  }
  if (state === 'error') {
    view.draft = question;
    view.notice = `Ошибка до начала ответа. Тестовый текст п. ${errorCode} — требует сверки с ФТ-7.`;
    view.tone = 'error';
    return view;
  }
  view.messages.push({ role: 'user', text: question });
  view.usage = 24;
  if (state === 'queued') {
    view.notice =
      'Вы в очереди: 3-й. Причина ожидания: все исполнители заняты.';
    return view;
  }
  const message: Message = { role: 'assistant', text: partial };
  view.messages.push(message);
  if (!busyStates.includes(state)) {
    view.draft = 'Что происходит, если один из узлов отключится?';
  }
  switch (state) {
    case 'streaming':
      message.streaming = true;
      break;
    case 'done':
      message.text = answer;
      message.footer = '18,4 т/с, прогноз 21,0 т/с';
      view.usage = 38;
      break;
    case 'stopped':
      message.footer = 'Ответ остановлен';
      break;
    case 'limit':
      message.text = answer;
      message.footer = 'Диалог достиг предела. Начните новый диалог';
      view.usage = 100;
      break;
    case 'rebuilding':
      message.streaming = true;
      view.notice = 'Рой перестраивается. Ожидайте продолжения ответа.';
      view.tone = 'warning';
      break;
    case 'retrying':
      message.text = '';
      message.streaming = true;
      view.notice = 'Рой перестроен. Ответ генерируется заново';
      break;
    case 'model-stopped':
      message.footer = 'Ответ не завершён';
      view.notice = 'Модель остановлена: недостаточно оперативной памяти роя';
      view.tone = 'error';
      break;
    case 'disconnected':
      message.footer = 'Ответ не завершён';
      view.notice = 'Связь с роем потеряна';
      view.tone = 'error';
      break;
    case 'disbanded':
      message.footer = 'Ответ не завершён';
      view.notice = 'Рой распущен';
      view.tone = 'warning';
      break;
  }
  return view;
}
