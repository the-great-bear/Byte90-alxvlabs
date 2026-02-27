import { defineConfig } from 'vite';
import mkcert from 'vite-plugin-mkcert';

// https://vite.dev/config/
export default defineConfig({
  plugins: [mkcert()],
  base: '/portal/',
  build: {
    assetsDir: '.',
  },
  server: {
    https: true,
  },
});
