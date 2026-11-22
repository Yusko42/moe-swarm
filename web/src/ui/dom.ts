export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  className = '',
  text?: string,
): HTMLElementTagNameMap[K] {
  const element = document.createElement(tag);
  element.className = className;
  if (text !== undefined) element.textContent = text;
  return element;
}

export function icon(
  name: 'plus' | 'send' | 'stop' | 'chat' | 'arrow',
): SVGSVGElement {
  const paths = {
    plus: 'M12 5v14M5 12h14',
    send: 'm5 12 7-7 7 7M12 5v14',
    stop: 'M7 7h10v10H7z',
    chat: 'M5 5h14v11H9l-4 4z',
    arrow: 'm8 5 7 7-7 7',
  };
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('fill', 'none');
  svg.setAttribute('stroke', 'currentColor');
  svg.setAttribute('stroke-width', '1.7');
  svg.setAttribute('stroke-linecap', 'round');
  svg.setAttribute('stroke-linejoin', 'round');
  svg.setAttribute('aria-hidden', 'true');
  const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
  path.setAttribute('d', paths[name]);
  svg.append(path);
  return svg;
}

export function logo(): HTMLElement {
  const mark = el('span', 'logo-mark');
  mark.setAttribute('aria-hidden', 'true');
  for (let i = 0; i < 3; i++) mark.append(el('i'));
  return mark;
}
