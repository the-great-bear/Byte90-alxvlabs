# BYTE-90 Captive Portal

Web UI for provisioning and configuring BYTE-90 devices.  
The portal is built with TypeScript + Vite and deployed to `data/portal/` for LittleFS hosting on the ESP32.

## Current Functionality

- WiFi management
- Scan nearby networks
- Connect/disconnect network
- Show current connection state
- OpenAI API key management
- Save/clear API key used by OpenAI Realtime
- API key card is always shown (not protocol-gated)
- Timezone and location settings
- Load timezone list
- Save/clear timezone and location
- Clock mode toggle
- Display customization
- Apply/clear effect (`none`, `scanlines`, `dot_matrix`, `glitch`)
- Apply tint (`none`, `green`, `blue`, `yellow`)
- Audio settings
- Apply/reset volume (`0-100`)
- Disable audio toggle

## API Endpoints Used By The Portal

- `GET /api/scan`
- `GET /api/status`
- `POST /api/configure`
- `POST /api/disconnect`
- `GET /api/openai-key/status`
- `POST /api/openai-key`
- `POST /api/openai-key/clear`
- `GET /api/timezone/list`
- `GET /api/timezone/status`
- `POST /api/timezone`
- `POST /api/timezone/clear`
- `GET /api/location/status`
- `POST /api/location`
- `POST /api/location/clear`
- `GET /api/clock/status`
- `POST /api/clock`
- `GET /api/effects/status`
- `POST /api/effects`
- `GET /api/audio/status`
- `POST /api/audio`
- `POST /api/audio/reset`

## Local Development

From repo root:

```bash
cd webserver
npm install
npm run dev
```

Vite runs with HTTPS (`vite-plugin-mkcert`) and `base: /portal/`.  
Open `https://localhost:5173/portal/`.

## Build And Deploy To Firmware Assets

From repo root:

```bash
cd webserver
npm run build
rsync -a --delete dist/ ../data/portal/
```

This updates the LittleFS-served portal files under `data/portal/`.

Note: `webserver/` is the source of truth. Do not hand-edit files in
`data/portal/` unless you intentionally need a one-off hotfix.

## Project Structure

```text
webserver/
  index.html
  src/main.ts
  src/portal.css
  src/assets/
  dist/                # build output (generated)
data/portal/           # firmware runtime portal assets
```

## Scripts

- `npm run dev` - Start Vite dev server
- `npm run build` - Type-check and build production assets
- `npm run preview` - Preview production build
- `npm run lint` - Run ESLint
