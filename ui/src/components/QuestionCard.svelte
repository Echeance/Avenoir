<script lang="ts">
  // Questions et confirmations du démon, toujours dessinées par Avenoir, hors
  // de tout contenu de module (directive de sécurité n° 13).
  import { isObject } from "../lib/protocol";
  import type { Question } from "../lib/session.svelte";
  import { plainText } from "../lib/text";

  let { question }: { question: Question } = $props();

  let typed = $state("");

  const text = $derived(
    typeof question.params["question"] === "string" ? plainText(question.params["question"]) : "",
  );
  const spec = $derived(isObject(question.params["spec"]) ? question.params["spec"] : {});
  const call = $derived(isObject(question.params["call"]) ? question.params["call"] : {});
  const choices = $derived(isObject(question.params["choices"]) ? question.params["choices"] : {});
  const risk = $derived(typeof spec["risk"] === "string" ? spec["risk"] : "sensitive");
  const riskLabel = $derived(
    risk === "read_only" ? "lecture seule" : risk === "reversible" ? "réversible" : "SENSIBLE",
  );

  function submitText(event: SubmitEvent): void {
    event.preventDefault();
    question.answer(question.method === "ask" ? { answer: typed } : typed);
  }

  function abandon(): void {
    question.answer(question.method === "ask" ? { answer: null } : null);
  }
</script>

<div class="card" role="alertdialog" aria-label="Question d'Avenoir">
  {#if question.method === "confirm"}
    <header>
      <span class="warning">▲ Action à confirmer</span>
      <span class="risk {risk}">{riskLabel}</span>
    </header>
    <p class="name">{typeof spec["name"] === "string" ? spec["name"] : ""}</p>
    <p class="description">
      {typeof spec["description"] === "string" ? plainText(spec["description"]) : ""}
    </p>
    <pre>{JSON.stringify(call["arguments"] ?? {}, null, 1)}</pre>
    <div class="buttons">
      <button class="primary" onclick={() => question.answer(true)}>Autoriser</button>
      <button onclick={() => question.answer(false)}>Refuser</button>
      {#if choices["always"] === true}
        <button onclick={() => question.answer("always")}>Toujours</button>
      {/if}
      {#if choices["never"] === true}
        <button onclick={() => question.answer("never")}>Jamais</button>
      {/if}
    </div>
  {:else if question.method === "ask_yes_no"}
    <p class="description">{text}</p>
    <div class="buttons">
      <button class="primary" onclick={() => question.answer(true)}>Oui</button>
      <button onclick={() => question.answer(false)}>Non</button>
    </div>
  {:else}
    <form onsubmit={submitText}>
      <p class="description">{text}</p>
      <!-- svelte-ignore a11y_autofocus -->
      <input
        type={question.method === "ask_secret" ? "password" : "text"}
        bind:value={typed}
        autocomplete="off"
        autofocus
        aria-label="Réponse"
      />
      <div class="buttons">
        <button class="primary" type="submit">Valider</button>
        <button type="button" onclick={abandon}>Abandonner</button>
      </div>
    </form>
  {/if}
</div>

<style>
  .card {
    margin: 8px 12px;
    padding: 10px 12px;
    border: 1px solid var(--yellow);
    border-radius: 8px;
    background: var(--night);
  }
  header {
    display: flex;
    justify-content: space-between;
    margin-bottom: 6px;
  }
  .warning {
    color: var(--yellow);
    font-weight: 600;
  }
  .risk {
    font-size: 12px;
    color: var(--green);
  }
  .risk.reversible {
    color: var(--yellow);
  }
  .risk.sensitive {
    color: var(--red);
    font-weight: 700;
  }
  p {
    margin: 0 0 6px;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
  }
  .name {
    font-weight: 700;
  }
  .description {
    font-size: 13px;
  }
  pre {
    margin: 0 0 8px;
    font-size: 12px;
    color: var(--muted);
    white-space: pre-wrap;
    overflow-wrap: anywhere;
    max-height: 120px;
    overflow-y: auto;
  }
  input {
    width: 100%;
    margin-bottom: 8px;
    padding: 6px 8px;
    border: 1px solid var(--line);
    border-radius: 6px;
    background: var(--panel);
    color: var(--text);
    font: inherit;
  }
  .buttons {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
  }
  button {
    background: transparent;
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 4px 12px;
  }
  .primary {
    background: var(--navy);
    border-color: var(--navy);
  }
</style>
