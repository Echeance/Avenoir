<script lang="ts">
  // La même page sert au widget et à l'application principale (« #app »,
  // ouverte par l'hôte avec --app).
  import MainApp from "./app/MainApp.svelte";
  import Widget from "./components/Widget.svelte";

  const application = location.hash.startsWith("#app");
  if (application) {
    document.documentElement.classList.add("application");
  }
</script>

{#if application}
  <MainApp />
{:else}
  <Widget />
{/if}
