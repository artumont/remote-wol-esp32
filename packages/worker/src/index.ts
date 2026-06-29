let esp32StreamController: ReadableStreamDefaultController | null = null;

enum OPERATION {
  HANDSHAKE = "HANDSHAKE",
  WAKE = "WAKE"
}

export default {
  async fetch(request, env): Promise<Response> {
    const url = new URL(request.url);
    const authHeader = request.headers.get("X-WOL-Secret");

    if (authHeader !== env.WOL_SECRET) {
      return new Response("Unauthorized", { status: 401 })
    }

    if (url.pathname === "/stream") {
      const stream = new ReadableStream({
        start(controller) {
          esp32StreamController = controller;
          controller.enqueue(new TextEncoder().encode(OPERATION.HANDSHAKE))
        },
        cancel() {
          esp32StreamController = null;
        }
      })
      return new Response(stream, {
        headers: {
          "Content-Type": "text/event-stream",
          "Cache-Control": "no-cache",
          "Connection": "keep-alive",
        }
      });
    }

    if (url.pathname === "/trigger" && request.method == "POST") {
      if (esp32StreamController) {
        try {
          esp32StreamController.enqueue(new TextEncoder().encode(OPERATION.WAKE))
          return Response.json({ success: true, message: "Wake command streamed to the controller" })
        } catch (e) {
          esp32StreamController = null;
          return Response.json({ success: false, error: "Stream link broken" }, { status: 500 });
        }
      }
      return Response.json({ success: false, error: "Controller is not currently connected" }, { status: 404 })
    }

    return new Response("Not Found", { status: 404 })
  },
} satisfies ExportedHandler<Env>;
