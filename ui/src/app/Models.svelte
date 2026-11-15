<script lang="ts">
  import { session } from "../lib/session.svelte";
  import { list, Report, str } from "./report.svelte";
  import Section from "./Section.svelte";

  const report = new Report("models/list");
  $effect(() => {
    if (session.connected) {
      void report.load();
    }
  });

  const installed = (tag: string): boolean =>
    list(report.data?.["installed"]).some((model) => {
      const name = str(model["tag"]);
      return name === tag || name === `${tag}:latest`;
    });
  // Un modèle configuré a son étiquette dans sa description (« nom : étiquette, local »).
  const configured = (tag: string): boolean =>
    list(report.data?.["configured"]).some((model) => str(model["description"]).includes(` ${tag},`));
  const gigabytes = (bytes: unknown): string =>
    typeof bytes === "number" ? `${(bytes / 1e9).toFixed(1).replace(".", ",")} Go` : "";
</script>

<Section
  title="Modèles"
  subtitle={`Modèles configurés, catalogue conseillé par le banc d'essai, modèles installés sur ${str(report.data?.["server"]) || "Ollama"}.`}
  error={report.error}
  onrefresh={() => void report.load()}
>
  <h2>Vos modèles</h2>
  <table>
    <tbody>
      {#each list(report.data?.["configured"]) as model (str(model["name"]))}
        <tr>
          <td><strong>{str(model["name"])}</strong>{#if model["active"] === true} <span class="active">● actif</span>{/if}</td>
          <td class="muted">{str(model["description"])}</td>
          <td>
            {#if model["active"] !== true}
              <button class="action" onclick={() => void report.run(`/modele ${str(model["name"])}`)}>Utiliser</button>
            {/if}
          </td>
        </tr>
      {:else}
        <tr><td class="muted">Aucun modèle : seule la voie rapide répond.</td></tr>
      {/each}
    </tbody>
  </table>

  <h2>Catalogue conseillé</h2>
  <table>
    <thead><tr><th>Modèle</th><th>Banc</th><th>Mémoire vidéo</th><th>Disque</th><th></th></tr></thead>
    <tbody>
      {#each list(report.data?.["catalog"]) as model (str(model["tag"]))}
        <tr>
          <td><strong>{str(model["label"])}</strong><div class="muted">{str(model["note"])}</div></td>
          <td>{str(model["score"])}</td>
          <td>≈ {str(model["video_memory_gb"])} Go</td>
          <td>≈ {str(model["disk_gb"])} Go</td>
          <td>
            {#if configured(str(model["tag"]))}
              <span class="muted">dans vos modèles</span>
            {:else}
              <button class="action" onclick={() => void report.run(`/modeles installer ${str(model["name"])}`)}>
                {installed(str(model["tag"])) ? "Ajouter" : "Installer"}
              </button>
            {/if}
          </td>
        </tr>
      {/each}
    </tbody>
  </table>

  <h2>Installés sur le serveur</h2>
  <table>
    <tbody>
      {#each list(report.data?.["installed"]) as model (str(model["tag"]))}
        <tr>
          <td><code>{str(model["tag"])}</code></td>
          <td class="muted">{gigabytes(model["size_bytes"])}</td>
          <td>
            {#if !configured(str(model["tag"]))}
              <button class="action" onclick={() => void report.run(`/modeles installer ${str(model["tag"])}`)}>Ajouter</button>
            {/if}
            <button class="action danger" onclick={() => void report.run(`/modeles retirer ${str(model["tag"])}`)}>Retirer</button>
          </td>
        </tr>
      {:else}
        <tr><td class="muted">Ollama ne répond pas, ou aucun modèle installé.</td></tr>
      {/each}
    </tbody>
  </table>
</Section>

<style>
  .active {
    color: var(--green);
    font-size: 12px;
  }
</style>
