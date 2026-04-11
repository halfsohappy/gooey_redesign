/* ── API helper ── */

import { toast } from "./toast.js";

export function api(endpoint, data, method) {
  const opts = {
    method: method || "POST",
    headers: { "Content-Type": "application/json" },
  };
  if (data) opts.body = JSON.stringify(data);
  return fetch("/api/" + endpoint, opts)
    .then(function (r) { return r.json(); })
    .then(function (res) {
      if (res.status === "error") toast(res.message, "error");
      return res;
    })
    .catch(function (err) {
      toast("Request failed: " + err.message, "error");
      return { status: "error", message: err.message };
    });
}
