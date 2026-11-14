import { svelte } from "@sveltejs/vite-plugin-svelte";
import type { Plugin } from "vite";
import { defineConfig } from "vitest/config";

// Politique de sécurité de la page livrée (directive n° 12) : aucun accès au
// réseau, aucun script ni style en ligne ; tout passe par l'hôte C++. Absente
// en développement (le serveur de Vite injecte ses propres scripts).
const CSP =
  "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; " +
  "font-src 'self'; connect-src 'none'; frame-src 'none'; base-uri 'none'; form-action 'none'";

function contentSecurityPolicy(): Plugin {
  return {
    name: "avenoir-csp",
    apply: "build",
    transformIndexHtml(html) {
      return html.replace(
        "<head>",
        `<head>\n    <meta http-equiv="Content-Security-Policy" content="${CSP}" />`,
      );
    },
  };
}

export default defineConfig({
  // Chemins relatifs : la page est servie par l'hôte sous avenoir://app/.
  base: "./",
  plugins: [svelte(), contentSecurityPolicy()],
  build: { target: "es2022", outDir: "dist", emptyOutDir: true, assetsInlineLimit: 0 },
  test: { environment: "node", include: ["tests/**/*.test.ts"] },
});
