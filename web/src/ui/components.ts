import { el, icon, logo } from './dom';
import { busyStates, stateNames, type Message, type ViewModel } from './states';

export function renderMessage(message: Message): HTMLElement {
  const article = el('article', `message message--${message.role}`);
  article.setAttribute(
    'aria-label',
    message.role === 'user' ? 'Ваш запрос' : 'Ответ роя',
  );
  const avatar = el('span', `avatar avatar--${message.role}`);
  avatar.setAttribute('aria-hidden', 'true');
  if (message.role === 'user') avatar.textContent = 'В';
  else avatar.append(logo());
  const content = el('div', 'message-content');
  content.append(
    el('div', 'message-author', message.role === 'user' ? 'Вы' : 'swarm'),
  );
  const text = el('div', 'message-text', message.text);
  if (message.streaming) {
    const caret = el('span', 'stream-caret');
    caret.setAttribute('aria-label', 'Ответ генерируется');
    text.append(caret);
  }
  content.append(text);
  if (message.footer) content.append(el('p', 'message-footer', message.footer));
  article.append(avatar, content);
  return article;
}

export function renderDialogue(messages: Message[]): HTMLElement {
  const region = el('div', 'dialogue');
  region.id = 'dialogue';
  region.setAttribute('role', 'log');
  region.setAttribute('aria-label', 'Диалог');
  region.setAttribute('aria-live', 'off');
  region.tabIndex = 0;
  if (!messages.length) {
    const welcome = el('div', 'welcome');
    const symbol = el('div', 'welcome-symbol');
    symbol.append(logo());
    welcome.append(
      symbol,
      el('p', 'eyebrow', 'ОБЩИЙ ИНТЕЛЛЕКТ. ВАШ ДИАЛОГ.'),
      el('h2', '', 'С чего начнём?'),
      el(
        'p',
        'welcome-description',
        'Задайте вопрос, разберите сложную тему\nили найдите новую идею вместе с роем.',
      ),
    );
    const cards = el('div', 'suggestions');
    // Декоративные примеры не притворяются интерактивными кнопками.
    for (const [title, description] of [
      ['Разобраться в теме', 'Объяснить сложное простыми словами'],
      ['Найти решение', 'Посмотреть на задачу с другой стороны'],
    ]) {
      const card = el('div', 'suggestion');
      card.append(
        icon('chat'),
        el('strong', '', title),
        el('span', '', description),
      );
      cards.append(card);
    }
    welcome.append(cards);
    region.append(welcome);
  } else {
    const list = el('div', 'message-list');
    for (const message of messages) list.append(renderMessage(message));
    region.append(list);
  }
  return region;
}

export function renderNotice(view: ViewModel): HTMLElement {
  const notice = el('div', `notice notice--${view.tone}`, view.notice);
  notice.id = 'notice';
  notice.setAttribute('role', view.tone === 'error' ? 'alert' : 'status');
  notice.setAttribute('aria-atomic', 'true');
  notice.hidden = !view.notice;
  return notice;
}

export function renderUsage(percent: number): HTMLElement {
  const usage = el('div', 'usage');
  const caption = el('span', '', `Диалог занят на ${percent} %`);
  const track = el('div', 'usage-track');
  track.setAttribute('role', 'progressbar');
  track.setAttribute('aria-label', 'Заполнение диалога');
  track.setAttribute('aria-valuenow', String(percent));
  track.setAttribute('aria-valuemin', '0');
  track.setAttribute('aria-valuemax', '100');
  const bar = el('span');
  bar.style.width = `${percent}%`;
  track.append(bar);
  usage.append(caption, track);
  return usage;
}

export function renderComposer(
  view: ViewModel,
  submit: () => void,
  stop: () => void,
): HTMLElement {
  const section = el('section', 'composer-section');
  const busy = busyStates.includes(view.state);
  section.append(renderNotice(view));
  const form = el('form', 'composer');
  const label = el('label', 'input-label', 'Запрос');
  label.htmlFor = 'prompt';
  const input = el('textarea');
  input.id = 'prompt';
  input.name = 'prompt';
  input.placeholder = 'Напишите сообщение…';
  input.value = view.draft;
  input.rows = 3;
  input.readOnly = busy;
  input.setAttribute('aria-describedby', 'input-hint');
  const bottom = el('div', 'composer-bottom');
  const hint = el(
    'span',
    'input-hint',
    'Enter — отправить · Shift + Enter — новая строка',
  );
  hint.id = 'input-hint';
  const labelText = busy
    ? view.state === 'queued'
      ? 'Отменить'
      : 'Остановить'
    : 'Отправить';
  const button = el(
    'button',
    busy ? 'primary-button primary-button--stop' : 'primary-button',
    labelText,
  );
  button.id = 'send';
  button.type = busy ? 'button' : 'submit';
  button.disabled = !busy && !view.draft.trim();
  button.append(icon(busy ? 'stop' : 'send'));
  input.addEventListener('input', () => {
    view.draft = input.value;
    if (!busy) button.disabled = !input.value.trim();
  });
  input.addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && !event.shiftKey && !event.isComposing) {
      event.preventDefault();
      if (!button.disabled && !busy) form.requestSubmit();
    }
  });
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    if (!busy) submit();
  });
  if (busy) button.addEventListener('click', stop);
  bottom.append(hint, button);
  form.append(label, input, bottom);
  section.append(form);
  return section;
}

export function renderHeader(
  view: ViewModel,
  newChat: () => void,
): HTMLElement {
  const header = el('header', 'chat-header');
  const titles = el('div');
  titles.append(
    el('p', 'eyebrow', 'РАБОЧЕЕ ПРОСТРАНСТВО'),
    el('h1', '', 'Чат с роем'),
  );
  const button = el('button', 'secondary-button', 'Новый диалог');
  button.id = 'new-chat';
  button.type = 'button';
  button.disabled = busyStates.includes(view.state);
  button.prepend(icon('plus'));
  button.addEventListener('click', newChat);
  header.append(titles, button);
  return header;
}

export function renderStateBadge(view: ViewModel): HTMLElement {
  const badge = el(
    'span',
    `state-badge${busyStates.includes(view.state) ? ' state-badge--busy' : ''}`,
    stateNames[view.state],
  );
  return badge;
}
