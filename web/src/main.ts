import './style.css';
import { el, icon, logo } from './ui/dom';
import {
  renderComposer,
  renderDialogue,
  renderHeader,
  renderStateBadge,
  renderUsage,
} from './ui/components';
import {
  fixture,
  isState,
  stateNames,
  type StateId,
  type ViewModel,
} from './ui/states';

const params = new URLSearchParams(location.search);
const live = params.get('mode') === 'live';
const requestedState = params.get('state');
let view = fixture(
  isState(requestedState) && !live ? requestedState : 'empty',
  ['5.2', '5.3', '5.4', '5.5'].includes(params.get('error') ?? '')
    ? params.get('error')!
    : '5.2',
);
const app = document.querySelector<HTMLDivElement>('#app')!;
let liveActions: { submit: () => void; stop: () => void } | undefined;

const sidebar = el('aside', 'sidebar');
const brand = el('div', 'brand');
brand.append(logo(), el('span', '', 'swarm'), el('span', 'brand-tag', 'chat'));
const sidebarTop = el('div');
sidebarTop.append(
  brand,
  el('p', 'sidebar-caption', ''),
);
const activeChat = el('div', 'nav-active');
activeChat.append(
  icon('chat'),
  el('span', '', 'Текущий диалог'),
  icon('arrow'),
);
sidebarTop.append(activeChat);
const sidebarBottom = el('div', 'sidebar-bottom');
sidebarBottom.append(
  el('span', 'local-dot'),
  el('span', '', 'Локальное пространство'),
);
sidebar.append(sidebarTop, sidebarBottom);

const main = el('main', 'main');
const demo = el('section', 'demo-toolbar');
demo.setAttribute('aria-label', 'Просмотр состояний');
const modeLabel = el(
  'span',
  'demo-label',
  live ? 'SSE · РЕЖИМ ПОДКЛЮЧЕНИЯ' : 'МАКЕТ · ПРОСМОТР СОСТОЯНИЙ',
);
const selectLabel = el('label', 'sr-only', 'Состояние страницы');
selectLabel.htmlFor = 'state';
const select = el('select');
select.id = 'state';
for (const [value, name] of Object.entries(stateNames)) {
  const option = el('option', '', name);
  option.value = value;
  select.append(option);
}
select.value = view.state;
select.hidden = live;
const errorSelect = el('select');
errorSelect.setAttribute('aria-label', 'Пункт ошибки ФТ-7');
for (const code of ['5.2', '5.3', '5.4', '5.5']) {
  const option = el('option', '', `Пункт ${code} (текст не утверждён)`);
  option.value = code;
  errorSelect.append(option);
}
errorSelect.value = params.get('error') ?? '5.2';
const modeLink = el('a', 'mode-link', live ? 'Все состояния' : 'Проверить SSE');
modeLink.href = live ? '?state=empty' : '?mode=live';
demo.append(modeLabel, selectLabel, select, errorSelect, modeLink);
const chat = el('div', 'chat');
main.append(demo, chat);
app.append(sidebar, main);

function changeState(state: StateId) {
  const url = new URL(location.href);
  url.searchParams.set('state', state);
  url.searchParams.delete('mode');
  if (state === 'error') url.searchParams.set('error', errorSelect.value);
  else url.searchParams.delete('error');
  history.replaceState(null, '', url);
  view = fixture(state, errorSelect.value);
  render();
}
select.addEventListener('change', () => changeState(select.value as StateId));
errorSelect.addEventListener('change', () => changeState('error'));

function render() {
  const previousDialogue = chat.querySelector<HTMLElement>('#dialogue');
  const scroll = previousDialogue?.scrollTop ?? 0;
  const nearBottom =
    !previousDialogue ||
    previousDialogue.scrollHeight -
      previousDialogue.scrollTop -
      previousDialogue.clientHeight <
      72;
  const active = document.activeElement;
  const focusedId = active instanceof HTMLElement ? active.id : '';
  const selection =
    active instanceof HTMLTextAreaElement
      ? [active.selectionStart, active.selectionEnd]
      : undefined;
  select.value = view.state;
  errorSelect.hidden = live || view.state !== 'error';
  chat.replaceChildren(
    renderHeader(view, () => {
      if (live) {
        view = fixture('empty');
        render();
      } else changeState('empty');
    }),
  );
  const status = el('div', 'chat-status');
  status.append(
    el('span', '', live ? 'Подключение через И-2' : 'Тестовые данные'),
    renderStateBadge(view),
  );
  const dialogue = renderDialogue(view.messages);
  chat.append(
    status,
    dialogue,
    renderUsage(view.usage),
    renderComposer(
      view,
      () => (live ? liveActions?.submit() : undefined),
      () => (live ? liveActions?.stop() : undefined),
    ),
  );
  chat.append(
    el(
      'p',
      'footer-note',
      live
        ? 'Ответы модели могут содержать ошибки. Проверяйте важную информацию.'
        : 'Статичный макет. Кнопки показаны для проверки вёрстки; сценарий выбирается сверху.',
    ),
  );
  dialogue.scrollTop = nearBottom ? dialogue.scrollHeight : scroll;
  if (focusedId) {
    const target = document.getElementById(focusedId);
    target?.focus({ preventScroll: true });
    if (selection && target instanceof HTMLTextAreaElement)
      target.setSelectionRange(selection[0], selection[1]);
  }
}

render();
if (live) {
  const { connectChat } = await import('./live');
  liveActions = connectChat(
    () => view,
    (next: ViewModel) => {
      view = next;
      render();
    },
  );
}
