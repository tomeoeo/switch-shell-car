// Screenshot every DIR/*.html at 1280x720 (the Switch screen). Uses the
// preinstalled Playwright Chromium: node tools/ui-preview/shoot.cjs DIR
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

(async () => {
	const dir = process.argv[2] || '.';
	const browser = await chromium.launch();
	const page = await browser.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1 });
	for (const f of fs.readdirSync(dir).filter((f) => f.endsWith('.html')).sort()) {
		await page.goto('file://' + path.resolve(dir, f));
		const out = path.join(dir, f.replace(/\.html$/, '.png'));
		await page.screenshot({ path: out });
		console.log(out);
	}
	await browser.close();
})();
