// @vitest-environment jsdom
import { afterEach, describe, expect, it, vi } from 'vitest';
import { busyStates, stateNames } from '../../src/ui/states';

afterEach(() => vi.unstubAllGlobals());

async function open(query: string) {
  document.body.innerHTML = '<div id="app"></div>';
  history.replaceState(null, '', query);
  vi.resetModules();
  await import('../../src/main');
}

describe('static page', () => {
  it.each(Object.keys(stateNames))(
    'renders %s with no requests',
    async (state) => {
      const fetch = vi.fn();
      vi.stubGlobal('fetch', fetch);
      await open(`?state=${state}`);
      expect(document.querySelector('h1')?.textContent).toBe('Чат с роем');
      const busy = busyStates.some((value) => value === state);
      const send = document.querySelector<HTMLButtonElement>('#send')!;
      expect(send.textContent).toBe(
        state === 'queued' ? 'Отменить' : busy ? 'Остановить' : 'Отправить',
      );
      expect(send.disabled).toBe(state === 'empty');
      expect(
        document.querySelector<HTMLButtonElement>('#new-chat')!.disabled,
      ).toBe(busy);
      expect(fetch).not.toHaveBeenCalled();
    },
  );

  it('updates whitespace validation and state URL, restores question for error', async () => {
    await open('?state=empty');
    const input = document.querySelector<HTMLTextAreaElement>('#prompt')!;
    const send = document.querySelector<HTMLButtonElement>('#send')!;
    input.value = '   ';
    input.dispatchEvent(new Event('input'));
    expect(send.disabled).toBe(true);
    input.value = 'Вопрос';
    input.dispatchEvent(new Event('input'));
    expect(send.disabled).toBe(false);
    const state = document.querySelector<HTMLSelectElement>('#state')!;
    state.value = 'error';
    state.dispatchEvent(new Event('change'));
    expect(location.search).toBe('?state=error&error=5.2');
    expect(
      document.querySelector<HTMLTextAreaElement>('#prompt')!.value,
    ).toContain('Объясни');
    document.querySelector<HTMLButtonElement>('#new-chat')!.click();
    expect(document.querySelector<HTMLSelectElement>('#state')!.value).toBe(
      'empty',
    );
  });

  it('shows exact supplied status texts and resets unfinished text on retry', async () => {
    await open('?state=stopped');
    expect(document.querySelector('.message-footer')!.textContent).toBe(
      'Ответ остановлен',
    );
    await open('?state=limit');
    expect(document.querySelector('.message-footer')!.textContent).toBe(
      'Диалог достиг предела. Начните новый диалог',
    );
    await open('?state=retrying');
    expect(document.querySelector('#notice')!.textContent).toBe(
      'Рой перестроен. Ответ генерируется заново',
    );
    expect(
      document.querySelector('.message--assistant .message-text')!.textContent,
    ).toBe('');
    await open('?state=model-stopped');
    expect(document.querySelector('#notice')!.textContent).toBe(
      'Модель остановлена: недостаточно оперативной памяти роя',
    );
    await open('?state=disconnected');
    expect(document.querySelector('#notice')!.textContent).toBe(
      'Связь с роем потеряна',
    );
  });

  it('renders untrusted message content as plain text', async () => {
    const { renderMessage } = await import('../../src/ui/components');
    const element = renderMessage({
      role: 'assistant',
      text: '<img src="https://example.com/x" onerror="alert(1)">',
    });
    expect(element.querySelector('img')).toBeNull();
    expect(element.querySelector('.message-text')?.textContent).toContain(
      '<img',
    );
  });
});
