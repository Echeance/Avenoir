<script lang="ts">
  // Onglet web d'un module (permission ui.web, directive 12) : sa page dans
  // une iframe « sandbox="allow-scripts" » (origine opaque : ni l'interface,
  // ni les autres modules), servie par l'hôte sous l'origine du module avec
  // une politique de sécurité sans réseau. Seul canal : postMessage — la page
  // lui donne les données de son module et relaie ses événements, validés.
  import { session } from "../lib/session.svelte";
  import { parseTabEvent } from "../lib/tabs";
  import type { Json } from "../lib/protocol";

  let { module, tab, title }: { module: string; tab: string; title: string } = $props();

  let frame: HTMLIFrameElement | undefined = $state();
  let error = $state("");
  const url = $derived(session.moduleUrl(module));

  function post(data: Json): void {
    // Origine opaque (sandbox) : « * » est la seule cible possible ; seule
    // cette iframe reçoit le message.
    frame?.contentWindow?.postMessage({ type: "avenoir/data", data }, "*");
  }

  async function load(): Promise<void> {
    try {
      const result = await session.request("tabs/content", { module, tab });
      post(result["data"] ?? null);
      error = "";
    } catch (failure) {
      error = failure instanceof Error ? failure.message : "onglet illisible";
    }
  }

  $effect(() => {
    const receive = (event: MessageEvent): void => {
      if (frame === undefined || event.source !== frame.contentWindow) {
        return; // pas cette iframe
      }
      const parsed = parseTabEvent(event.data);
      if (parsed === null) {
        return;
      }
      void session
        .request("tabs/event", { module, tab, name: parsed.name, payload: parsed.payload })
        .then((result) => post(result["data"] ?? null))
        .catch((failure: unknown) => {
          error = failure instanceof Error ? failure.message : "événement refusé";
        });
    };
    window.addEventListener("message", receive);
    return () => window.removeEventListener("message", receive);
  });
</script>

<section class="web">
  {#if url === null}
    <p class="muted">Onglets web indisponibles ici.</p>
  {:else}
    <iframe
      bind:this={frame}
      src={url}
      {title}
      sandbox="allow-scripts"
      referrerpolicy="no-referrer"
      onload={() => void load()}
    ></iframe>
  {/if}
  {#if error !== ""}<p class="warning">{error}</p>{/if}
</section>

<style>
  .web {
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
  }
  iframe {
    flex: 1;
    border: 0;
    width: 100%;
    background: transparent;
  }
  .muted {
    color: var(--muted);
    padding: 10px 14px;
  }
  .warning {
    color: var(--yellow);
    padding: 4px 14px;
    margin: 0;
  }
</style>
