<script lang="ts">
  // Onglet « Activité » du widget (décision 78) : ce qu'Avenoir fait en ce
  // moment (requête, projets en parallèle) et l'historique de ses actions.
  import { session } from "../lib/session.svelte";
  import type { JsonObject } from "../lib/protocol";

  let status = $state<JsonObject | null>(null);
  let actions = $state<JsonObject[]>([]);
  let error = $state("");

  const str = (value: unknown): string => (typeof value === "string" ? value : "");
  const objects = (value: unknown): JsonObject[] =>
    Array.isArray(value)
      ? value.filter((item): item is JsonObject => typeof item === "object" && item !== null && !Array.isArray(item))
      : [];

  async function load(): Promise<void> {
    try {
      const [state, journal] = await Promise.all([
        session.request("status/list"),
        session.request("journal/list", { all: true, limit: 30 }),
      ]);
      status = state;
      actions = objects(journal["actions"]);
      error = "";
    } catch (failure) {
      error = failure instanceof Error ? failure.message : "lecture impossible";
    }
  }

  // Relu à l'ouverture, à la fin de chaque requête, et toutes les 5 s tant
  // que l'onglet est affiché (projets en parallèle).
  $effect(() => {
    void session.running;
    void load();
    const timer = setInterval(() => void load(), 5000);
    return () => clearInterval(timer);
  });

  const projects = $derived(objects(status?.["projects"]));
</script>

<section class="tab">
  {#if error !== ""}<p class="warning">{error}</p>{/if}
  <h2>En ce moment</h2>
  {#if session.view.thinking}
    <p>⏳ {session.view.activity}…</p>
  {:else}
    <p class="muted">Rien en cours.</p>
  {/if}
  {#each projects as project (str(project["name"]))}
    <p>
      {project["running"] === true ? "⏳" : "✓"} projet <strong>{str(project["name"])}</strong>
      <span class="muted">— {str(project["request"])}</span>
    </p>
  {/each}
  <h2>Dernières actions</h2>
  {#each actions as action (String(action["id"]))}
    <p class="action">
      <span class="mark">{action["executed"] === true ? (action["succeeded"] === true ? "✓" : "✗") : "–"}</span>
      <span class="time">{str(action["at"]).slice(11, 16)}</span>
      <code>{str(action["tool"])}</code>
      <span class="muted result">{str(action["result"]) || str(action["reason"])}</span>
    </p>
  {:else}
    <p class="muted">Aucune action.</p>
  {/each}
</section>

<style>
  .tab {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    padding: 8px 14px 10px;
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  h2 {
    margin: 6px 0 2px;
    font-size: 13px;
    color: var(--ice);
  }
  p {
    margin: 0;
    overflow-wrap: anywhere;
  }
  .muted {
    color: var(--muted);
  }
  .warning {
    color: var(--yellow);
  }
  .action {
    display: flex;
    gap: 8px;
    font-size: 12px;
    align-items: baseline;
  }
  .time {
    color: var(--muted);
  }
  .result {
    flex: 1;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
</style>
