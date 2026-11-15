<script lang="ts">
  // Logs d'Avenoir (démon) et des modules (leur sortie d'erreur), en lecture
  // seule : la fin du fichier, 64 Kio au plus. Les secrets n'y sont jamais
  // écrits.
  import { session } from "../lib/session.svelte";
  import { Report, str } from "./report.svelte";
  import Section from "./Section.svelte";

  let source = $state("daemon");
  const report = new Report("logs/list", () => ({ source }));
  let box: HTMLPreElement | undefined = $state();

  $effect(() => {
    void source;
    if (session.connected) {
      void report.load().then(() => {
        if (box !== undefined) {
          box.scrollTop = box.scrollHeight; // les dernières lignes
        }
      });
    }
  });

  const sources = $derived(
    Array.isArray(report.data?.["sources"]) ? report.data["sources"].map((one) => str(one)) : ["daemon"],
  );
  const text = $derived(str(report.data?.["text"]));
</script>

<Section
  title="Logs"
  subtitle="Fin des logs d'Avenoir et des modules ; AVENOIR_LOG=debug pour plus de détails."
  error={report.error}
  onrefresh={() => void report.load()}
>
  <label class="muted">
    Source
    <select bind:value={source}>
      {#each sources as one (one)}
        <option value={one}>{one === "daemon" ? "Avenoir (démon)" : `module ${one}`}</option>
      {/each}
    </select>
  </label>
  <pre bind:this={box}>{text === "" ? "Log vide." : text}</pre>
</Section>

<style>
  select {
    margin-left: 8px;
    background: var(--night);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 2px 6px;
  }
  pre {
    margin-top: 10px;
    max-height: 520px;
    overflow: auto;
    background: var(--night);
    border: 1px solid var(--line);
    border-radius: 8px;
    padding: 10px 12px;
    font-size: 12px;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
  }
</style>
