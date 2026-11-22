import { expect, test } from '@playwright/test';
import { busyStates, stateNames } from '../../src/ui/states';

for (const [state, name] of Object.entries(stateNames)) {
  test(`static: ${name}`, async ({ page, baseURL }) => {
    const apiRequests: string[] = [];
    const externalRequests: string[] = [];
    page.on('request', (request) => {
      if (new URL(request.url()).pathname.startsWith('/api/'))
        apiRequests.push(request.url());
      if (new URL(request.url()).origin !== new URL(baseURL!).origin)
        externalRequests.push(request.url());
    });
    await page.goto(`/?state=${state}`);
    await expect(
      page.getByRole('heading', { name: 'Чат с роем' }),
    ).toBeVisible();
    await expect(page.locator('#state')).toHaveValue(state);
    const busy = busyStates.some((value) => value === state);
    if (busy) await expect(page.locator('#new-chat')).toBeDisabled();
    else await expect(page.locator('#new-chat')).toBeEnabled();
    await expect(page.locator('#send')).toHaveText(
      state === 'queued' ? 'Отменить' : busy ? 'Остановить' : 'Отправить',
    );
    if (state === 'empty') await expect(page.locator('#send')).toBeDisabled();
    else await expect(page.locator('#send')).toBeEnabled();
    if (state === 'retrying')
      await expect(
        page.locator('.message--assistant .message-text'),
      ).toHaveText('');
    if (state === 'done')
      await expect(page.locator('.message-footer')).toHaveText(
        '18,4 т/с, прогноз 21,0 т/с',
      );
    if (state === 'stopped')
      await expect(page.locator('.message-footer')).toHaveText(
        'Ответ остановлен',
      );
    if (state === 'limit')
      await expect(page.locator('.message-footer')).toHaveText(
        'Диалог достиг предела. Начните новый диалог',
      );
    if (state === 'model-stopped')
      await expect(page.locator('#notice')).toHaveText(
        'Модель остановлена: недостаточно оперативной памяти роя',
      );
    if (state === 'disconnected')
      await expect(page.locator('#notice')).toHaveText('Связь с роем потеряна');
    if (state === 'disbanded')
      await expect(page.locator('#notice')).toHaveText('Рой распущен');
    expect(apiRequests).toEqual([]);
    expect(externalRequests).toEqual([]);
  });
}

test('state switch, draft, disabled send and new dialogue', async ({
  page,
}) => {
  await page.goto('/');
  await expect(page.locator('#send')).toBeDisabled();
  await page.locator('#prompt').fill('  ');
  await expect(page.locator('#send')).toBeDisabled();
  await page.locator('#prompt').fill('Вопрос');
  await expect(page.locator('#send')).toBeEnabled();
  await page.locator('#state').selectOption('error');
  await expect(page.locator('#prompt')).toHaveValue(/Объясни/);
  await page
    .getByRole('combobox', { name: 'Пункт ошибки ФТ-7' })
    .selectOption('5.5');
  await expect(page.locator('#notice')).toContainText('5.5');
  await page.reload();
  await expect(page.locator('#notice')).toContainText('5.5');
  await page.locator('#new-chat').click();
  await expect(page.locator('#state')).toHaveValue('empty');
  await expect(page.locator('#prompt')).toHaveValue('');
});

test('long words wrap and long answers scroll on mobile', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto('/?state=done');
  await page.locator('.message--assistant .message-text').evaluate((node) => {
    node.textContent = 'x'.repeat(20_000);
  });
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  expect(
    await page
      .locator('#dialogue')
      .evaluate((node) => node.scrollHeight > node.clientHeight),
  ).toBe(true);
  await expect(page.locator('#prompt')).toBeVisible();
  await expect(page.locator('#send')).toBeInViewport();
});

test('POST mock streams accepted → started → token → done', async ({
  page,
}, info) => {
  test.skip(info.project.name !== 'dev', 'Mock is deliberately absent in dist');
  await page.goto('/?mode=live');
  await page.locator('#prompt').fill('Как работает рой?');
  const requestPromise = page.waitForRequest('/api/v1/chat');
  await page.locator('#send').click();
  expect((await requestPromise).method()).toBe('POST');
  await expect(page.locator('#notice')).toContainText('Вы в очереди: 2-й');
  await expect(page.locator('#send')).toHaveText('Отменить');
  await expect(page.locator('#new-chat')).toBeDisabled();
  await expect(page.locator('#send')).toHaveText('Остановить');
  await expect(page.locator('.message--assistant .message-text')).toContainText(
    'Рой',
  );
  await expect(page.locator('.message-footer')).toHaveText(
    '13,3 т/с, прогноз 15,2 т/с',
    { timeout: 15_000 },
  );
  await expect(page.locator('#new-chat')).toBeEnabled();
});

test('stop preserves partial answer and cancels browser stream', async ({
  page,
}, info) => {
  test.skip(info.project.name !== 'dev');
  await page.goto('/?mode=live');
  await page.locator('#prompt').fill('Тест остановки');
  await page.locator('#send').click();
  await expect(page.locator('.message--assistant .message-text')).toContainText(
    'Рой',
  );
  await page.locator('#send').click();
  const partial = await page
    .locator('.message--assistant .message-text')
    .textContent();
  await expect(page.locator('.message-footer')).toHaveText('Ответ остановлен');
  await expect(page.locator('.message--assistant .message-text')).toHaveText(
    partial!,
  );
  await expect(page.locator('#new-chat')).toBeEnabled();
});

test('failure before started restores prompt', async ({ page }, info) => {
  test.skip(info.project.name !== 'dev');
  await page.route('**/api/v1/chat', (route) =>
    route.fulfill({ status: 503, body: 'Unavailable' }),
  );
  await page.goto('/?mode=live');
  await page.locator('#prompt').fill('Верни этот вопрос');
  await page.locator('#send').click();
  await expect(page.locator('#notice')).toContainText('HTTP 503');
  await expect(page.locator('#prompt')).toHaveValue('Верни этот вопрос');
});
