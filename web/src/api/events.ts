/** Временный контракт И-2. Все wire-типы и их проверка живут здесь. */
export interface ChatRequest {
  prompt: string;
  conversation_id?: string;
}

export type ChatEvent =
  | { type: 'accepted'; request_id: string; position: number; reason: string }
  | { type: 'started'; request_id: string }
  | { type: 'token'; text: string }
  | {
      type: 'done';
      tokens_per_second: number;
      predicted_tokens_per_second: number;
      context_percent: number;
    };

export interface StateResponse {
  status: 'ready' | 'busy';
}

export interface SwarmResponse {
  status: 'active' | 'disbanded';
  nodes: number;
}

export function decodeEvent(type: string, data: string): ChatEvent {
  const value: unknown = JSON.parse(data);
  if (typeof value !== 'object' || value === null)
    throw new Error('Некорректное событие И-2');
  const record = value as Record<string, unknown>;
  const string = (key: string) => {
    if (typeof record[key] !== 'string')
      throw new Error(`И-2: отсутствует строка ${key}`);
    return record[key];
  };
  const number = (key: string) => {
    if (
      typeof record[key] !== 'number' ||
      !Number.isFinite(record[key]) ||
      record[key] < 0
    )
      throw new Error(`И-2: некорректное число ${key}`);
    return record[key];
  };
  switch (type) {
    case 'accepted':
      return {
        type,
        request_id: string('request_id'),
        position: number('position'),
        reason: string('reason'),
      };
    case 'started':
      return { type, request_id: string('request_id') };
    case 'token':
      return { type, text: string('text') };
    case 'done': {
      const context = number('context_percent');
      if (context > 100) throw new Error('И-2: заполнение диалога выше 100 %');
      return {
        type,
        tokens_per_second: number('tokens_per_second'),
        predicted_tokens_per_second: number('predicted_tokens_per_second'),
        context_percent: context,
      };
    }
    default:
      throw new Error(`Неизвестное событие И-2: ${type}`);
  }
}
