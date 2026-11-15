<script lang="ts">
  // Onglet « Système » du widget (décision 78) : version d'Avenoir et mises
  // à jour (vérifiées seulement sur demande : une requête à GitHub), machine,
  // modèle, modules, consommation de la conversation.
  import { formatCost, formatDuration, formatTokens } from "../lib/format";
  import type { JsonObject } from "../lib/protocol";
  import { session } from "../lib/session.svelte";

  let status = $state<JsonObject | null>(null);
  let update = $state<JsonObject | null>(null);
  let checking = $state(false);
  let error = $state("");

  const str = (value: unknown): string => (typeof value === "string" ? value : "");
  const num = (value: unknown): number => (typeof value === "number" ? value : 0);
  const object = (value: unknown): JsonObject | null =>
    typeof value === "object" && value !== null && !Array.isArray(value) ? (value as JsonObject) : null;

  $effect(() => {
    void session.running;
    void session
      .request("status/list")
      .then((result) => {
        status = result;
        error = "";
      })
      .catch((failure: unknown) => {
        error = failure instanceof Error ? failure.message : "lecture impossible";
      });
  });

  async function check(): Promise<void> {
    checking = true;
    try {
      update = await session.request("update/list");
    } catch (failure) {
      update = { error: failure instanceof Error ? failure.message : "vérification impossible" };
    } finally {
      checking = false;
    }
  }

  const model = $derived(object(status?.["model"]));
  const usage = $derived(object(status?.["usage"]));
  const modules = $derived(
    Array.isArray(status?.["modules"]) ? status["modules"].map((item) => object(item)).filter((item) => item !== null) : [],
  );
</script>

<section class="tab">
  {#if error !== ""}<p class="warning">{error}</p>{/if}
  <p class="stat"><span class="label">Version</span><span>Avenoir {str(status?.["version"])}</span></p>
  <p class="stat">
    <span class="label">Mises à jour</span>
    <span>
      {#if update === null}
        <button class="action" disabled={checking} onclick={() => void check()}>{checking ? "Vérification…" : "Vérifier"}</button>
        <span class="muted small">interroge GitHub</span>
      {:else if str(update["error"]) !== ""}
        <span class="warning">{str(update["error"])}</span>
      {:else if update["available"] === true}
        <span class="new">Version {str(update["latest"])} disponible</span>
        {#if str(update["url"]) !== ""}<span class="muted small"> — {str(update["url"])}</span>{/if}
      {:else}
        À jour{#if str(update["latest"]) !== ""}<span class="muted"> (dernière publiée : {str(update["latest"])})</span>{/if}
      {/if}
    </span>
  </p>
  <p class="stat"><span class="label">Machine</span><span>{str(status?.["system"]).replace(/^Système : /, "")}</span></p>
  <p class="stat"><span class="label">Démarré il y a</span><span>{formatDuration(num(status?.["uptime_s"]))}</span></p>
  {#if model !== null}
    <p class="stat">
      <span class="label">Modèle</span>
      <span>{str(model["name"])}{#if str(model["model"]) !== ""}&nbsp;<span class="muted">({str(model["model"])})</span>{/if} · {model["local"] === true ? "local" : "distant"}</span>
    </p>
  {/if}
  <p class="stat">
    <span class="label">Modules</span>
    <span>
      {#each modules as module, index (str(module["id"]))}{index > 0 ? ", " : ""}<span class:muted={module["running"] !== true}>{str(module["name"])}</span>{:else}aucun{/each}
    </span>
  </p>
  <p class="stat">
    <span class="label">Consommation</span>
    <span>
      {#if usage !== null && num(usage["answers"]) > 0}
        {formatTokens(num(usage["input_tokens"]))} jetons envoyés, {formatTokens(num(usage["output_tokens"]))} reçus
        {#if typeof usage["cost_usd"] === "number"} · {formatCost(usage["cost_usd"])}{/if}
        <span class="muted small">(cette conversation)</span>
      {:else}
        <span class="muted">non communiquée par ce modèle</span>
      {/if}
    </span>
  </p>
  {#if model?.["claude_code"] === true}
    <p class="muted small">Claude Code ne communique pas ses limites d'abonnement : voyez-les avec /usage dans Claude Code.</p>
  {/if}
</section>

<style>
  .tab {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    padding: 10px 14px;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  p {
    margin: 0;
    overflow-wrap: anywhere;
  }
  .stat {
    display: flex;
    gap: 10px;
  }
  .label {
    color: var(--muted);
    min-width: 120px;
    flex-shrink: 0;
  }
  .muted {
    color: var(--muted);
  }
  .small {
    font-size: 12px;
  }
  .warning {
    color: var(--yellow);
  }
  .new {
    color: var(--green);
  }
  .action {
    background: var(--navy);
    border: 0;
    border-radius: 6px;
    padding: 2px 10px;
  }
</style>
