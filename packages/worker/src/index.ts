let pendingWake = false;

enum OPERATION {
  WAKE = "WAKE",
}

export default {
  async fetch(request, env): Promise<Response> {
    const url = new URL(request.url);
    const authHeader = request.headers.get("X-WOL-Secret");

    if (authHeader !== env.WOL_SECRET) {
      return new Response("Unauthorized", { status: 401 });
    }

    if (url.pathname === "/poll") {
      if (pendingWake) {
        pendingWake = false;
        return Response.json({ operation: OPERATION.WAKE });
      }
      return Response.json({ operation: null });
    }

    if (url.pathname === "/trigger" && request.method == "POST") {
      pendingWake = true;
      return Response.json({
        success: true,
        message: "Wake command queued for the controller",
      });
    }

    return new Response("Not Found", { status: 404 });
  },
} satisfies ExportedHandler<Env>;
