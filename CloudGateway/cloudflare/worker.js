// Bind this Worker to timiac.it/* and set ORIGIN_URL to the Cloud Run HTTPS URL.
// The Worker preserves browser authentication headers while fetching the origin
// under its original Cloud Run host name.
export default {
  async fetch(request, env) {
    const origin = new URL(env.ORIGIN_URL);
    const incoming = new URL(request.url);
    origin.pathname = incoming.pathname; origin.search = incoming.search;
    const headers = new Headers(request.headers);
    headers.set('Host', origin.host);
    return fetch(new Request(origin, { method: request.method, headers, body: request.body, redirect: 'manual' }));
  }
};
