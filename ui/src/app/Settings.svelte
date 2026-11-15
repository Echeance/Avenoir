<script lang="ts">
  import { session } from "../lib/session.svelte";
  import { list, Report, str } from "./report.svelte";
  import Section from "./Section.svelte";

  const report = new Report("settings/list");
  let editing = $state<string | null>(null);
  let value = $state("");

  $effect(() => {
    if (session.connected) {
      void report.load();
    }
  });

  async function apply(key: string): Promise<void> {
    const chosen = value.trim();
    if (chosen === "") {
      return;
    }
    editing = null;
    await report.run(`/reglage ${key} ${chosen}`);
  }
</script>

<Section
  title="Réglages"
  subtitle="Changés par Avenoir, seul rédacteur des réglages scellés. ▲ : l'affaiblir demande votre accord. Pour appliquer : Redémarrer Avenoir."
  error={report.error}
  onrefresh={() => void report.load()}
>
  <p><button class="action" onclick={() => void session.send("/redemarrer")}>Redémarrer Avenoir</button></p>
  <table>
    <thead><tr><th>Réglage</th><th>Valeur</th><th></th></tr></thead>
    <tbody>
      {#each list(report.data?.["settings"]) as setting (str(setting["key"]))}
        {@const key = str(setting["key"])}
        <tr>
          <td>
            {#if setting["security"] === true}<span class="security" title="sécurité">▲</span>{/if}
            <code>{key}</code>
            <div class="muted">{str(setting["description"])}</div>
          </td>
          <td>
            {#if editing === key}
              <form onsubmit={(event) => { event.preventDefault(); void apply(key); }}>
                <!-- svelte-ignore a11y_autofocus -->
                <input bind:value autofocus aria-label={key} />
                <div class="muted">{str(setting["accepted"])}</div>
              </form>
            {:else}
              <strong>{str(setting["value"])}</strong>
            {/if}
          </td>
          <td>
            {#if editing === key}
              <button class="action" onclick={() => void apply(key)}>Appliquer</button>
              <button class="action" onclick={() => (editing = null)}>Annuler</button>
            {:else}
              <button class="action" onclick={() => { editing = key; value = str(setting["value"]); }}>Changer</button>
            {/if}
          </td>
        </tr>
      {/each}
    </tbody>
  </table>
</Section>

<style>
  .security {
    color: var(--yellow);
    margin-right: 4px;
  }
  input {
    width: 100%;
    padding: 4px 8px;
    border: 1px solid var(--line);
    border-radius: 6px;
    background: var(--panel);
    color: var(--text);
    font: inherit;
  }
</style>
