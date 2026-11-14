// Pont entre la page et l'hôte C++ des fenêtres. Selon la vue web : WebKitGTK
// (`window.webkit.messageHandlers.avenoir`), WebView2 (`window.chrome.webview`)
// ou, dans un navigateur ordinaire (développement), un hôte simulé.

import {
  parseHostMessage,
  type CallMethod,
  type HostMessage,
  type Json,
  type JsonObject,
  type PageMessage,
} from "./protocol";

export interface Transport {
  send(message: PageMessage): void;
  /// Appelée une fois : `receive` reçoit ensuite chaque message de l'hôte.
  listen(receive: (raw: unknown) => void): void;
}

export interface WebKitWindow {
  webkit?: { messageHandlers?: { avenoir?: { postMessage(message: string): void } } };
  chrome?: {
    webview?: {
      postMessage(message: string): void;
      addEventListener(type: "message", listener: (event: { data: unknown }) => void): void;
    };
  };
  /// Entrée de l'hôte WebKitGTK (appelée par `evaluate_javascript`).
  avenoirReceive?: (message: unknown) => void;
}

/// Transport de la vue web en cours, ou `null` dans un navigateur ordinaire.
export function nativeTransport(target: WebKitWindow): Transport | null {
  const webkit = target.webkit?.messageHandlers?.avenoir;
  if (webkit !== undefined) {
    return {
      send: (message) => webkit.postMessage(JSON.stringify(message)),
      listen: (receive) => {
        target.avenoirReceive = receive;
      },
    };
  }
  const webview = target.chrome?.webview;
  if (webview !== undefined) {
    return {
      send: (message) => webview.postMessage(JSON.stringify(message)),
      listen: (receive) => webview.addEventListener("message", (event) => receive(event.data)),
    };
  }
  return null;
}

export interface BridgeHandlers {
  ui(params: JsonObject): void;
  /// Question du démon : la promesse donne la réponse à lui rendre.
  question(method: Extract<HostMessage, { type: "question" }>["method"], params: JsonObject): Promise<Json>;
  status(connected: boolean, message: string): void;
}

/// Appels de la page vers le démon, et distribution de ce que l'hôte envoie.
export class Bridge {
  private nextId = 1;
  private readonly pending = new Map<
    number,
    { resolve: (result: JsonObject) => void; reject: (error: Error) => void }
  >();

  constructor(
    private readonly transport: Transport,
    private readonly handlers: BridgeHandlers,
  ) {
    transport.listen((raw) => this.receive(raw));
    transport.send({ type: "ready" });
  }

  call(method: CallMethod, params: JsonObject = {}): { id: number; result: Promise<JsonObject> } {
    const id = this.nextId++;
    const result = new Promise<JsonObject>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
    });
    this.transport.send({ type: "call", id, method, params });
    return { id, result };
  }

  /// Interrompt l'appel `id` (équivalent de Ctrl+C).
  cancel(id: number): void {
    this.transport.send({ type: "cancel", id });
  }

  private receive(raw: unknown): void {
    // WebView2 livre un objet ou une chaîne JSON selon l'appel de l'hôte.
    let value: unknown = raw;
    if (typeof raw === "string") {
      try {
        value = JSON.parse(raw);
      } catch {
        return;
      }
    }
    const message = parseHostMessage(value);
    if (message === null) {
      return;
    }
    switch (message.type) {
      case "ui":
        this.handlers.ui(message.params);
        break;
      case "status":
        this.handlers.status(message.connected, message.message);
        break;
      case "question":
        void this.handlers
          .question(message.method, message.params)
          .catch((): Json => null)
          .then((result) => this.transport.send({ type: "answer", id: message.id, result }));
        break;
      case "reply":
        this.pending.get(message.id)?.resolve(message.result);
        this.pending.delete(message.id);
        break;
      case "failure":
        this.pending.get(message.id)?.reject(new Error(message.message));
        this.pending.delete(message.id);
        break;
    }
  }
}
